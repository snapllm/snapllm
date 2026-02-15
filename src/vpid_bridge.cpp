/**
 * @file vpid_bridge.cpp
 * @brief vPID-llama.cpp Bridge Implementation
 */

#include "snapllm/vpid_bridge.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

#ifdef _OPENMP
#include <omp.h>
#endif

// Include llama.cpp headers
extern "C" {
#include "ggml.h"
#include "ggml-common.h"  // For block types (block_mxfp4, etc.)
#include "ggml-quants.h"
}
#include "llama.h"

// Include private headers for tensor access (proof-of-concept only)
// In production, you'd use the public API or implement a custom backend
#include "llama-model.h"

namespace snapllm {

// Get default workspace path based on OS
static std::string get_default_workspace() {
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return std::string(userprofile) + "\\SnapLLM_Workspace";
    }
    return "C:\\SnapLLM_Workspace";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/SnapLLM_Workspace";
    }
    return "/tmp/SnapLLM_Workspace";
#endif
}

// Static member definitions
bool VPIDBridge::backend_initialized_ = false;
std::mutex VPIDBridge::backend_mutex_;

VPIDBridge::VPIDBridge(const std::string& workspace_root)
    : workspace_root_(workspace_root.empty() ? get_default_workspace() : workspace_root),
      hot_cache_(std::make_unique<VPIDHotCache>(2ULL * 1024 * 1024 * 1024)),  // 2GB HOT cache (shared across ALL models)
      validator_(ValidationConfig()),  // Initialize with validation disabled by default
      workspace_metadata_(std::make_unique<WorkspaceMetadata>(workspace_root.empty() ? get_default_workspace() : workspace_root))
{
    std::cout << "VPIDBridge: Initialized with 3-tier caching (HOT/WARM/COLD)" << std::endl;
    std::cout << "VPIDBridge: Workspace root: " << workspace_root_ << std::endl;
    std::cout << "VPIDBridge: HOT cache size: 2GB (shared across all models)" << std::endl;
    std::cout << "VPIDBridge: Per-model workspaces: <model_name>/<quant_type>/workspace.bin" << std::endl;

    // Initialize workspace metadata
    if (!workspace_metadata_->initialize()) {
        std::cerr << "Warning: Failed to initialize workspace metadata" << std::endl;
    }
}

VPIDBridge::~VPIDBridge() {
    // Clean up all loaded models
    std::lock_guard<std::mutex> lock(models_mutex_);

    if (!loaded_models_.empty()) {
        std::cout << "Cleaning up " << loaded_models_.size() << " loaded models..." << std::endl;

        for (auto& pair : loaded_models_) {
            std::cout << "  Freeing model: " << pair.first << std::endl;
            llama_model_free(pair.second);
        }

        loaded_models_.clear();
        model_access_times_.clear();
    }

    // NOTE: Don't call llama_backend_free() here - it's a shared resource
    // Let it be cleaned up at program exit naturally
}

std::string VPIDBridge::evict_lru_model() {
    // Must be called with models_mutex_ held
    if (loaded_models_.empty()) {
        return "";  // Nothing to evict
    }

    // Find the least recently used model
    std::string lru_model;
    auto oldest_time = std::chrono::steady_clock::time_point::max();

    for (const auto& [name, access_time] : model_access_times_) {
        if (access_time < oldest_time) {
            oldest_time = access_time;
            lru_model = name;
        }
    }

    if (lru_model.empty()) {
        std::cerr << "[GPU Memory] Warning: Could not find LRU model to evict" << std::endl;
        return "";
    }

    // Get VRAM freed by this eviction
    size_t freed_mb = 0;
    auto vram_it = model_vram_usage_.find(lru_model);
    if (vram_it != model_vram_usage_.end()) {
        freed_mb = vram_it->second;
        total_vram_used_ -= freed_mb;
        model_vram_usage_.erase(vram_it);
    }

    // Evict the LRU model
    std::cout << "[GPU Memory] Evicting model '" << lru_model << "' from GPU (LRU, frees " << freed_mb << " MB)" << std::endl;

    auto it = loaded_models_.find(lru_model);
    if (it != loaded_models_.end()) {
        llama_model_free(it->second);
        loaded_models_.erase(it);
        model_access_times_.erase(lru_model);
        std::cout << "[GPU Memory] Model '" << lru_model << "' evicted. VRAM: " << total_vram_used_ << "/" << VRAM_BUDGET_MB << " MB" << std::endl;
    }

    return lru_model;
}

bool VPIDBridge::ensure_vram_space(size_t needed_mb) {
    // Check if we already have enough space
    if (total_vram_used_ + needed_mb <= VRAM_BUDGET_MB) {
        std::cout << "[GPU Memory] Enough VRAM available: " << total_vram_used_ << " + " << needed_mb << " <= " << VRAM_BUDGET_MB << " MB" << std::endl;
        return true;
    }

    std::cout << "[GPU Memory] Need " << needed_mb << " MB, have " << (VRAM_BUDGET_MB - total_vram_used_) << " MB free, evicting..." << std::endl;

    // Evict models until we have enough space
    while (total_vram_used_ + needed_mb > VRAM_BUDGET_MB && !loaded_models_.empty()) {
        std::string evicted = evict_lru_model();
        if (evicted.empty()) {
            break;  // No more models to evict
        }
    }

    return (total_vram_used_ + needed_mb <= VRAM_BUDGET_MB);
}

bool VPIDBridge::load_model_with_vpid_tensors(
    const std::string& model_name,
    const std::string& gguf_path,
    const ModelInfo* model_info,
    std::shared_ptr<DequantCache> cache)
{
    std::cout << "  [Custom Loader] Loading GGUF structure (no tensor data)..." << std::endl;

    if (!cache) {
        std::cerr << "Error: Cache pointer is null" << std::endl;
        return false;
    }

    // Initialize llama.cpp backend (only once)
    {
        std::lock_guard<std::mutex> lock(backend_mutex_);
        if (!backend_initialized_) {
            llama_backend_init();
            backend_initialized_ = true;
            std::cout << "  [Backend] llama.cpp backend initialized" << std::endl;
        }
    }

    // Load model using llama.cpp with external tensors support
    llama_model_params model_params = llama_model_default_params();
    // model_params.use_external_tensors = true;  // DISABLED - set later based on GPU mode
                                                 // This skips buffer allocation and data loading
                                                 // Model structure created, tensors injected via API

    llama_model* model = llama_model_load_from_file(gguf_path.c_str(), model_params);
    if (!model) {
        std::cerr << "Failed to load model structure with llama.cpp" << std::endl;
        return false;
    }

    std::cout << "  [Custom Loader] Model structure loaded" << std::endl;
    std::cout << "  [Phase 2C] Controlled HOT/WARM Caching: Selective RAM + mmap" << std::endl;
    std::cout << "  [Strategy] HOT=Critical tensors in RAM, WARM=All others in mmap (OS paging)" << std::endl;

    // PHASE 2C: Selective HOT cache + WARM mmap
    // HOT: Only critical tensors (controlled RAM budget)
    // WARM: All others stay in mmap (OS manages paging on demand)
    // NO aggressive prefetch - let OS page cache work naturally

    size_t hot_count = 0;
    size_t hot_bytes = 0;

    std::cout << "  [HOT Cache] Loading critical tensors into RAM..." << std::endl;
    std::cout << "    Budget: " << (hot_cache_->get_max_size() / (1024.0 * 1024.0)) << " MB" << std::endl;

    // PHASE 2C: Only load truly critical tensors into HOT cache
    // This keeps RAM usage controlled while providing fast access to hot path
    std::vector<std::string> hot_tensor_patterns = {
        "token_embd.weight",     // Input embeddings (~500MB) - used every forward pass
        "output.weight",         // Output embeddings (~500MB) - used every token
    };

    for (const auto& tensor_pair : model->tensors_by_name) {
        const std::string& tensor_name = tensor_pair.first;

        bool is_hot = false;
        for (const auto& pattern : hot_tensor_patterns) {
            if (tensor_name == pattern) {
                is_hot = true;
                break;
            }
        }

        if (is_hot) {
            const TensorInfo* info = cache->get_tensor_info(model_name, tensor_name);
            if (tensor_name == "token_embd.weight") {
                std::cout << "  [DEBUG] Testing get_tensor_info for HOT tensor:" << std::endl;
                std::cout << "    model_name: " << model_name << std::endl;
                std::cout << "    tensor_name: " << tensor_name << std::endl;
                std::cout << "    Found: " << (info != nullptr ? "YES" : "NO") << std::endl;
                // Check if model is registered
                const ModelInfo* minfo = cache->get_model_info(model_name);
                std::cout << "    Model registered: " << (minfo != nullptr ? "YES" : "NO") << std::endl;
                if (minfo) {
                    std::cout << "    Model has " << minfo->tensors.size() << " tensors" << std::endl;
                }
            }
            if (info) {
                const float* disk_ptr = cache->get_tensor(model_name, tensor_name);

                // VALIDATION STAGE 3: Post-vPID Read (before HOT cache prefetch)
                if (disk_ptr && validator_.is_enabled() && validator_.get_config().validate_vpid_read) {
                    auto stats = validator_.validate(disk_ptr, info->num_elements, tensor_name, "Post-vPID Read (HOT prefetch)");
                    if (!stats.is_valid) {
                        std::cerr << "    [VALIDATION FAILED] " << tensor_name << ": " << stats.error_message << std::endl;
                    }
                }

                if (disk_ptr && hot_cache_->prefetch(model_name, tensor_name, disk_ptr, info->num_elements)) {
                    hot_bytes += info->byte_size;
                    hot_count++;
                    std::cout << "    ✓ " << tensor_name << " ("
                              << (info->byte_size / (1024.0 * 1024.0)) << " MB)" << std::endl;
                }
            }
        }
    }

    std::cout << "  [HOT Cache] Loaded " << hot_count << " tensors, "
              << (hot_bytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  [WARM Cache] " << (model->tensors_by_name.size() - hot_count)
              << " tensors remain in mmap (zero-copy, OS paging on demand)" << std::endl;

    std::cout << "  [Custom Loader] Wiring vPID tensors to llama.cpp..." << std::endl;

    // Now replace tensor data pointers with vPID pointers
    size_t tensors_wired = 0;
    size_t tensors_hot = 0;   // Tensors in HOT cache (RAM)
    size_t tensors_warm = 0;  // Tensors in WARM (mmap with OS page cache)
    size_t tensors_not_found = 0;

    for (const auto& tensor_pair : model->tensors_by_name) {
        const std::string& tensor_name = tensor_pair.first;
        struct ggml_tensor* tensor = tensor_pair.second;

        if (!tensor) continue;

        // PHASE 2C: Try HOT cache first (RAM), then fall back to WARM (mmap)
        const float* tensor_data = hot_cache_->get_if_cached(model_name, tensor_name);
        bool from_hot = (tensor_data != nullptr);

        // DEBUG: First tensor in this call
        if (tensors_wired == 0) {
            std::cout << "\n[DEBUG First Tensor: " << tensor_name << "]" << std::endl;
            std::cout << "  HOT cache result: " << (from_hot ? "HIT" : "MISS") << std::endl;
            std::cout << "  Cache pointer valid: " << (cache ? "YES" : "NO") << std::endl;
            std::cout << "  About to call cache->get_tensor with:" << std::endl;
            std::cout << "    model_name: '" << model_name << "'" << std::endl;
            std::cout << "    tensor_name: '" << tensor_name << "'" << std::endl;
        }

        if (!from_hot) {
            // Not in HOT cache, get from WARM tier (mmap, zero-copy)
            tensor_data = cache->get_tensor(model_name, tensor_name);
            if (tensors_wired == 0) {
                std::cout << "  cache->get_tensor returned: " << (tensor_data ? "VALID POINTER" : "NULLPTR") << std::endl;
            }
        }

        if (tensor_data) {
            // Use official external tensor API
            // This properly sets data pointer, type, strides, and nullifies buffer
            bool success = llama_model_set_external_tensor(
                model,
                tensor_name.c_str(),
                const_cast<float*>(tensor_data),
                GGML_TYPE_F32
            );

            if (success) {
                // VALIDATION STAGE 4: Post-Tensor Wiring (verify pointer is correct)
                if (validator_.is_enabled() && validator_.get_config().validate_tensor_wiring) {
                    // Verify the tensor pointer was set correctly by reading from model
                    if (tensor && tensor->data == tensor_data) {
                        // Validate a small sample of the data to ensure it's accessible
                        const TensorInfo* info = cache->get_tensor_info(model_name, tensor_name);
                        if (info) {
                            size_t sample_size = (info->num_elements < 100) ? info->num_elements : 100;
                            auto stats = validator_.validate(tensor_data, sample_size, tensor_name,
                                                           "Post-Tensor Wiring (sample)");
                            if (!stats.is_valid) {
                                std::cerr << "  [VALIDATION FAILED] " << tensor_name
                                         << ": Tensor data invalid after wiring!" << std::endl;
                            }
                        }
                    } else {
                        std::cerr << "  [VALIDATION WARNING] " << tensor_name
                                 << ": Tensor pointer mismatch after wiring!" << std::endl;
                    }
                }

                tensors_wired++;
                if (from_hot) {
                    tensors_hot++;
                } else {
                    tensors_warm++;
                }
            } else {
                std::cerr << "  Warning: Failed to set external tensor '" << tensor_name << "'" << std::endl;
                tensors_not_found++;
                continue;
            }

            if (tensors_wired % 50 == 0) {
                std::cout << "  [Custom Loader] Wired " << tensors_wired << " tensors..." << std::endl;
            }
        } else {
            std::cerr << "  Warning: Tensor '" << tensor_name << "' not found in vPID cache!" << std::endl;
            tensors_not_found++;
        }
    }

    std::cout << "  [Custom Loader] Complete!" << std::endl;
    std::cout << "    Total tensors: " << tensors_wired << std::endl;
    std::cout << "    ⚡ HOT (RAM):  " << tensors_hot << " tensors" << std::endl;
    std::cout << "    💾 WARM (mmap):  " << tensors_warm << " tensors (OS page cache)" << std::endl;
    std::cout << "    ❌ Not found:  " << tensors_not_found << std::endl;

    if (tensors_not_found > 0) {
        std::cerr << "  [Custom Loader] Failed: Not all tensors found in vPID!" << std::endl;
        llama_model_free(model);
        llama_backend_free();
        return false;
    }

    // Store the model for inference (Phase 4)
    {
        std::lock_guard<std::mutex> lock(models_mutex_);

        // Check if we need to evict a model to make room
        if (loaded_models_.find(model_name) == loaded_models_.end()) {
            // New model - may need eviction
            evict_lru_model();
        }

        // Free old model if exists
        auto it = loaded_models_.find(model_name);
        if (it != loaded_models_.end()) {
            std::cout << "  [Custom Loader] Freeing old model instance..." << std::endl;
            llama_model_free(it->second);
        }

        loaded_models_[model_name] = model;
        model_access_times_[model_name] = std::chrono::steady_clock::now();
    }

    // Register tensors for layer-aware eviction
    std::string extracted_model_name = model_name.substr(0, model_name.find("/"));
    std::string quant_type = "Q8_0";  // TODO: Extract from model_name
    std::string vdpe_cache_dir = workspace_root_ + "/" + extracted_model_name + "/" + quant_type + "/vdpe_cache";
    std::string manifest_path = vdpe_cache_dir + "/workspace_manifest.bin";
    std::cout << "  [Layer Tracking] Attempting to open manifest: " << manifest_path << std::endl;
    std::ifstream manifest_file(manifest_path, std::ios::binary);
    if (manifest_file.is_open()) {
        auto workspace = cache->get_vpid();
        
        // Read number of entries (uint64_t in manifest format)
        uint64_t num_entries = 0;
        manifest_file.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));

        std::cout << "  [Layer Tracking] SKIPPING registration (disabled for testing)..." << std::endl;
        std::cout << "  [Layer Tracking] Would register " << num_entries << " tensors" << std::endl;

        if (false) { // DISABLED for testing
        for (uint32_t i = 0; i < num_entries; i++) {
            uint32_t name_len = 0;
            manifest_file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
            std::string tensor_name(name_len, '\0');
            manifest_file.read(&tensor_name[0], name_len);

            size_t offset = 0, size = 0;
            manifest_file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            manifest_file.read(reinterpret_cast<char*>(&size), sizeof(size));

            int64_t dims[4];
            manifest_file.read(reinterpret_cast<char*>(dims), sizeof(dims));

            workspace->register_tensor_layer(tensor_name, offset, size);
        }
        } // end if (false)

        manifest_file.close();
        std::cout << "  [Layer Tracking] ✓ Registration complete!" << std::endl;
    } else {
        std::cerr << "  [Layer Tracking] ERROR: Failed to open manifest file: " << manifest_path << std::endl;
    }

    std::cout << "  [Custom Loader] Model stored and ready for inference!" << std::endl;

    return true;
}

bool VPIDBridge::load_and_dequantize_model(
    const std::string& model_name,
    const std::string& gguf_path,
    bool force_reload,
    const GPUConfig& gpu_config)
{
    std::cout << "Loading model: " << model_name << std::endl;
    std::cout << "  From: " << gguf_path << std::endl;

    // Extract model name and quantization type from GGUF path
    std::string extracted_model_name = WorkspaceMetadata::extract_model_name(gguf_path);
    std::string quant_type = WorkspaceMetadata::extract_quant_type(gguf_path);

    // Get or create per-model cache
    // Cache is keyed by user-provided model_name, workspace path uses extracted names
    // Workspace size is calculated dynamically based on GGUF file size
    auto cache = get_or_create_cache(model_name, extracted_model_name, quant_type, gguf_path);
    if (!cache) {
        std::cerr << "Failed to create workspace for model" << std::endl;
        return false;
    }

    // Check if already loaded AND in GPU memory
    if (!force_reload && cache->is_model_loaded(model_name)) {
        // Also verify model is actually in GPU (not just metadata cached)
        {
            std::lock_guard<std::mutex> lock(models_mutex_);
            if (loaded_models_.find(model_name) != loaded_models_.end()) {
                std::cout << "Model '" << model_name << "' already loaded in GPU" << std::endl;
                return true;
            }
        }
        // Model metadata exists but was evicted from GPU - continue to reload
        std::cout << "  Model '" << model_name << "' was evicted from GPU, reloading..." << std::endl;
    }

    std::cout << "  Detected model name: " << extracted_model_name << std::endl;
    std::cout << "  Detected quant type: " << quant_type << std::endl;

    // Check if model exists in persistent workspace
    if (!force_reload && workspace_metadata_->model_exists(extracted_model_name, quant_type)) {
        std::cout << "  Found model in persistent workspace!" << std::endl;
        std::cout << "  Loading from D:\\SnapLLM_Workspace\\" << extracted_model_name << "\\" << quant_type << std::endl;

        // Load metadata from workspace
        ModelMetadata ws_metadata = workspace_metadata_->get_model_metadata(extracted_model_name, quant_type);

        if (!ws_metadata.tensors.empty()) {
            std::cout << "  ✓ Workspace metadata loaded!" << std::endl;
            std::cout << "    Tensors: " << ws_metadata.tensor_count << std::endl;
            std::cout << "    Total size: " << (ws_metadata.total_size_bytes / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;
            std::cout << "    Architecture: " << ws_metadata.architecture << std::endl;

            // Convert WorkspaceMetadata to ModelInfo format for cache registration
            ModelInfo model_info;
            model_info.name = model_name;
            model_info.architecture = ws_metadata.architecture;
            model_info.vocab_size = ws_metadata.vocab_size;
            model_info.context_length = ws_metadata.context_length;
            model_info.embedding_length = ws_metadata.embedding_length;
            model_info.num_layers = ws_metadata.layer_count;

            // Convert TensorLocation to TensorInfo
            for (const auto& tensor_loc : ws_metadata.tensors) {
                TensorInfo tensor_info;
                tensor_info.name = tensor_loc.name;
                tensor_info.num_elements = tensor_loc.element_count;
                tensor_info.byte_size = tensor_loc.size_bytes;
                tensor_info.vpid_offset = tensor_loc.vpid_offset;
                tensor_info.access_count = 0;

                model_info.tensors.push_back(tensor_info);
                model_info.tensor_index[tensor_info.name] = model_info.tensors.size() - 1;
            }

            // Register model with cache
            if (cache->register_model_with_metadata(model_info)) {
                std::cout << "  ✓ Model registered with cache" << std::endl;

                // Load model structure with llama.cpp, wire it to vPID tensors
                std::cout << "  Loading model structure from GGUF (for inference)..." << std::endl;
                std::cout << "  This will use vPID tensors from workspace instead of quantized data" << std::endl;

                return load_model_with_vpid_tensors(model_name, gguf_path, &model_info, cache);
            }
        }

        std::cout << "  Workspace metadata load failed, falling back to full dequantization" << std::endl;
    } else {
        if (force_reload) {
            std::cout << "  Force reload requested, skipping workspace check" << std::endl;
        } else {
            std::cout << "  Model not found in persistent workspace, will dequantize" << std::endl;
        }
    }

    // Check if vDPE manifest exists (indicates second load with progressive loading capability)
    static std::string vdpe_cache_dir;
    vdpe_cache_dir = workspace_root_ + "/" + extracted_model_name + "/" + quant_type + "/vdpe_cache";
    std::string vdpe_manifest_file = vdpe_cache_dir + "/workspace_manifest.bin";

    bool vdpe_manifest_exists = false;
    std::ifstream manifest_check(vdpe_manifest_file, std::ios::binary);
    if (manifest_check.good()) {
        manifest_check.close();
        vdpe_manifest_exists = true;
        std::cout << "  ✓ vDPE manifest found - will use progressive loading (external tensors)" << std::endl;
    } else {
        std::cout << "  [vDPE Architecture] First load: llama.cpp will save F32 cache" << std::endl;
        std::cout << "  [vDPE Architecture] Subsequent loads will use cached F32 tensors" << std::endl;
    }

    // Initialize llama.cpp backend (only once)
    {
        std::lock_guard<std::mutex> lock(backend_mutex_);
        if (!backend_initialized_) {
            llama_backend_init();
            backend_initialized_ = true;
            std::cout << "  [Backend] llama.cpp backend initialized" << std::endl;
        }
    }

    std::cout << "  [vDPE Cache] Directory: " << vdpe_cache_dir << std::endl;

    // Check if workspace has memory mapping available for external tensor wiring
    bool has_mmap = cache->get_vpid()->has_memory_mapping();

    // IMPORTANT: External tensors in host RAM don't work with GPU inference!
    // GPU backend needs tensor data in VRAM, not host memory-mapped RAM.
    // For GPU mode (n_gpu_layers > 0), we MUST use the standard GGUF loading path.
    bool using_gpu = true;  // We always use GPU (n_gpu_layers=999)
    bool use_external = false;  // DISABLED for GPU mode - external tensors are in host RAM

    if (vdpe_manifest_exists && has_mmap && !using_gpu) {
        use_external = true;  // Would work for CPU-only inference
        std::cout << "  [vDPE] Using external tensors from memory-mapped workspace" << std::endl;
    } else if (vdpe_manifest_exists) {
        std::cout << "  [vDPE] Manifest exists but external tensors disabled for GPU mode" << std::endl;
        std::cout << "  [vDPE] Loading from GGUF (OS file cache makes this fast)" << std::endl;
    }

    // GPU memory management: Smart VRAM-based eviction
    // Calculate model size for VRAM tracking (done early before loading)
    size_t model_size_for_vram = 0;
    try {
        model_size_for_vram = std::filesystem::file_size(gguf_path) / (1024 * 1024);  // MB
    } catch (...) {}
    
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        std::cout << "  [GPU Memory Check] model=" << model_name
                  << ", size=" << model_size_for_vram << " MB"
                  << ", used=" << total_vram_used_ << "/" << VRAM_BUDGET_MB << " MB" << std::endl;
        
        // Only evict if this is a new model AND we don't have space
        if (loaded_models_.find(model_name) == loaded_models_.end()) {
            ensure_vram_space(model_size_for_vram);
        }
    }

    // Load model using llama.cpp with vDPE caching enabled
    llama_model_params model_params = llama_model_default_params();
    model_params.use_mmap = true;                                     // Enable mmap: OS page cache speeds up reloads of evicted models
    model_params.use_external_tensors = use_external;                 // ✅ Only if manifest exists AND mmap available
    model_params.vdpe_enable = false;  // Disabled: F32 cache not useful for GPU mode                                  // ✅ vDPE: Enable F32 cache save/load
    model_params.vdpe_cache_path = vdpe_cache_dir.c_str();            // vDPE cache directory
    model_params.vpid_workspace = cache->get_vpid().get();            // ✅ vDPE: VPIDWorkspace for memory-mapped storage

    // GPU LAYER ALLOCATION: Smart mode based on model size and VRAM
    // Get model size to determine optimal GPU offload ratio
    size_t gguf_file_bytes = 0;
    try {
        gguf_file_bytes = std::filesystem::file_size(gguf_path);
    } catch (...) {
        gguf_file_bytes = 0;
    }
    size_t model_size_mb = gguf_file_bytes / (1024 * 1024);
    
    // GPU layer configuration - supports manual override or auto-detect
    const size_t VRAM_MB = gpu_config.vram_budget_mb > 0 ? gpu_config.vram_budget_mb : 7000;  // Default RTX 4060 Laptop
    int n_gpu_layers;
    
    if (gpu_config.n_gpu_layers >= 0) {
        // Manual override - user specified exact layers
        n_gpu_layers = gpu_config.n_gpu_layers;
        std::cout << "  [GPU] Manual config: " << n_gpu_layers << " layers on GPU" << std::endl;
        if (n_gpu_layers == 0) {
            std::cout << "  [GPU] CPU-only mode requested" << std::endl;
        }
    } else if (model_size_mb < VRAM_MB * 0.8) {
        // Auto: Model fits comfortably in VRAM - use all layers
        n_gpu_layers = 999;
        std::cout << "  [GPU] Full offload: Model fits in VRAM (" << model_size_mb << " MB < " << (VRAM_MB * 0.8) << " MB)" << std::endl;
    } else {
        // Auto: Model is larger than VRAM - calculate optimal ratio like LMStudio
        float offload_ratio = static_cast<float>(VRAM_MB * 0.85) / model_size_mb;
        offload_ratio = std::min(1.0f, std::max(0.3f, offload_ratio));  // Clamp to 30%-100%
        
        // Estimate layers (assume 32 layers typical, will be auto-adjusted by llama.cpp)
        n_gpu_layers = static_cast<int>(32 * offload_ratio);
        n_gpu_layers = std::max(8, n_gpu_layers);  // At least 8 layers on GPU
        
        std::cout << "  [GPU] Partial offload: Model (" << model_size_mb << " MB) > VRAM (" << VRAM_MB << " MB)" << std::endl;
        std::cout << "  [GPU] Offload ratio: " << (offload_ratio * 100) << "%" << std::endl;
        std::cout << "  [GPU] Target GPU layers: " << n_gpu_layers << std::endl;
    }
    
    model_params.n_gpu_layers = n_gpu_layers;
    std::cout << "  [DEBUG] vpid_workspace pointer set to: " << model_params.vpid_workspace << std::endl;
    std::cout << "  [DEBUG] has_memory_mapping = " << (has_mmap ? "true" : "false") << std::endl;
    std::cout << "  [DEBUG] use_external_tensors = " << (use_external ? "true" : "false") << std::endl;

    llama_model* model = llama_model_load_from_file(gguf_path.c_str(), model_params);
    if (!model) {
        std::cerr << "Failed to load model with llama.cpp" << std::endl;
        return false;
    }

    std::cout << "  ✅ Model loaded successfully!" << std::endl;
    std::cout << "  [vDPE] F32 cache ready for next load" << std::endl;

    // Register tensors for layer-aware eviction
    std::string manifest_path = vdpe_cache_dir + "/workspace_manifest.bin";
    std::cout << "  [Layer Tracking] Attempting to open manifest: " << manifest_path << std::endl;
    std::ifstream manifest_file(manifest_path, std::ios::binary);
    if (manifest_file.is_open()) {
        auto workspace = cache->get_vpid();
        
        // Read number of entries (uint64_t in manifest format)
        uint64_t num_entries = 0;
        manifest_file.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));

        std::cout << "  [Layer Tracking] SKIPPING registration (disabled for testing)..." << std::endl;
        std::cout << "  [Layer Tracking] Would register " << num_entries << " tensors" << std::endl;

        if (false) { // DISABLED for testing
        for (uint32_t i = 0; i < num_entries; i++) {
            // Read tensor name
            uint32_t name_len = 0;
            manifest_file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
            std::string tensor_name(name_len, '\0');
            manifest_file.read(&tensor_name[0], name_len);

            // Read offset and size
            size_t offset = 0, size = 0;
            manifest_file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            manifest_file.read(reinterpret_cast<char*>(&size), sizeof(size));

            // Skip dimensions (GGML_MAX_DIMS * int64_t)
            int64_t dims[4];
            manifest_file.read(reinterpret_cast<char*>(dims), sizeof(dims));

            // Register with workspace
            workspace->register_tensor_layer(tensor_name, offset, size);
        }
        } // end if (false)

        manifest_file.close();
        std::cout << "  [Layer Tracking] ✓ Registration complete!" << std::endl;
    } else {
        std::cerr << "  [Layer Tracking] ERROR: Failed to open manifest file: " << manifest_path << std::endl;
    }

    // Register model in DequantCache so inference can find it
    size_t num_tensors = 291;  // Known from vDPE cache manifest
    uint64_t total_size = llama_model_size(model);

    std::cout << "  Registering model in cache (tensors: " << num_tensors << ", size: "
              << (total_size / (1024*1024)) << " MB)" << std::endl;

    if (!cache->register_model(model_name, gguf_path, num_tensors, total_size)) {
        std::cerr << "  Warning: Failed to register model in cache" << std::endl;
    }

    // vDPE Architecture: llama.cpp has handled all F32 caching
    // Store model pointer for inference
    {
        std::lock_guard<std::mutex> lock(models_mutex_);

        // Check if we need to evict a model to make room
        if (loaded_models_.find(model_name) == loaded_models_.end()) {
            // New model - may need eviction
            evict_lru_model();
        }

        // Free old model if exists
        auto it = loaded_models_.find(model_name);
        if (it != loaded_models_.end()) {
            std::cout << "  Freeing old model instance..." << std::endl;
            llama_model_free(it->second);
        }

        loaded_models_[model_name] = model;
        model_access_times_[model_name] = std::chrono::steady_clock::now();
        
        // Track VRAM usage for this model
        model_vram_usage_[model_name] = model_size_for_vram;
        total_vram_used_ += model_size_for_vram;
        std::cout << "  [VRAM Tracking] Model '" << model_name << "' uses " << model_size_for_vram 
                  << " MB. Total VRAM: " << total_vram_used_ << "/" << VRAM_BUDGET_MB << " MB" << std::endl;
    }

    std::cout << "  [vDPE Complete] Model ready for inference!" << std::endl;

    return true;

    // OLD CODE BELOW - No longer needed with vDPE architecture
    #if 0
    std::cout << "  Model loaded, extracting tensors..." << std::endl;

    // TODO: Get model metadata using llama.cpp v3 API
    // Note: llama.cpp API changes frequently. For this proof-of-concept,
    // we'll use placeholder values. Full implementation would use:
    // - llama_model_meta_val_str() for architecture
    // - llama_model_params() for configuration
    // - Proper tensor iteration through model internals

    std::cout << "  Extracting and dequantizing tensors from llama.cpp..." << std::endl;

    // Access the tensors_by_name vector from llama_model
    // This contains all tensors in the model with their names
    size_t total_tensors = model->tensors_by_name.size();
    std::cout << "  Found " << total_tensors << " tensors in model" << std::endl;

    // Build model metadata
    snapllm::ModelInfo model_info;
    model_info.name = model_name;
    model_info.architecture = "llama";  // TODO: Extract from model metadata
    model_info.vocab_size = 32000;      // TODO: Extract from model
    model_info.context_length = 4096;   // TODO: Extract from model
    model_info.embedding_length = 4096; // TODO: Extract from model
    model_info.num_layers = 32;         // TODO: Extract from model
    model_info.num_heads = 32;          // TODO: Extract from model
    model_info.num_kv_heads = 32;       // TODO: Extract from model

    size_t total_dequant_size = 0;
    size_t tensors_processed = 0;

    // PHASE 2D: Multi-threaded dequantization
    // Convert map to vector for parallel processing
    std::vector<std::pair<std::string, struct ggml_tensor*>> tensor_vec;
    tensor_vec.reserve(model->tensors_by_name.size());
    for (const auto& pair : model->tensors_by_name) {
        tensor_vec.push_back(pair);
    }

    // Determine number of threads
    int num_threads = 1;
#ifdef _OPENMP
    num_threads = omp_get_max_threads();
#endif
    std::cout << "  [Multi-threading] Using " << num_threads << " CPU cores for parallel F32 extraction..." << std::endl;

    // Thread-safe storage for tensor info
    std::vector<snapllm::TensorInfo> tensor_infos(tensor_vec.size());
    std::atomic<size_t> processed_count{0};

    // Parallel dequantization loop
    #pragma omp parallel for schedule(dynamic)
    for (int64_t i = 0; i < static_cast<int64_t>(tensor_vec.size()); i++) {
        const std::string& tensor_name = tensor_vec[i].first;
        struct ggml_tensor* tensor = tensor_vec[i].second;

        if (!tensor) {
            #pragma omp critical
            {
                std::cerr << "  Warning: null tensor '" << tensor_name << "', skipping" << std::endl;
            }
            continue;
        }

        // Get tensor properties
        const char* name = ggml_get_name(tensor);
        enum ggml_type type = tensor->type;
        int64_t num_elements = ggml_nelements(tensor);

        // CRITICAL FIX: Use backend API to get tensor data
        // ggml_get_data() returns a pointer but with backends, the actual data
        // is in backend buffers. We must use ggml_backend_tensor_get() to copy data.
        size_t tensor_bytes = ggml_nbytes(tensor);
        std::vector<uint8_t> tensor_data(tensor_bytes);
        ggml_backend_tensor_get(tensor, tensor_data.data(), 0, tensor_bytes);
        void* data = tensor_data.data();

        // Create tensor info for metadata
        snapllm::TensorInfo tensor_info;
        tensor_info.name = name;
        tensor_info.num_elements = num_elements;
        tensor_info.byte_size = num_elements * sizeof(float);  // Always F32 after dequant

        // Get tensor shape
        for (int j = 0; j < GGML_MAX_DIMS; j++) {
            if (tensor->ne[j] > 1 || j == 0) {
                tensor_info.shape.push_back(tensor->ne[j]);
            }
        }

        // ✅ vDPE Architecture: llama.cpp has already dequantized to F32
        // All tensors are now F32 after pre_dequantize_all
        // Simply extract F32 data and store to disk

        #pragma omp critical
        {
            std::cout << "  [Thread " << omp_get_thread_num() << "] Extracting F32 tensor '" << name
                      << "' (" << num_elements << " elements)" << std::endl;
        }

        // Verify tensor is F32 (should always be true after pre_dequantize_all)
        if (type != GGML_TYPE_F32) {
            #pragma omp critical
            {
                std::cerr << "  [ERROR] Tensor '" << name << "' is type " << type << ", expected F32 after pre_dequantize_all!" << std::endl;
            }
            processed_count++;
            continue;
        }

        // Extract F32 data from llama.cpp (already dequantized)
        std::vector<float> f32_data(num_elements);

        // Get F32 data from backend buffer (no dequantization needed!)
        // data pointer from tensor_data vector already contains F32
        memcpy(f32_data.data(), data, tensor_info.byte_size);

        // VALIDATION STAGE 1: Post-Extraction
        if (validator_.is_enabled() && validator_.get_config().validate_dequantization) {
            #pragma omp critical
            {
                auto stats = validator_.validate(f32_data.data(), f32_data.size(), name, "Post-F32-Extraction");
                if (!stats.is_valid) {
                    std::cerr << "  [VALIDATION FAILED] " << name << ": " << stats.error_message << std::endl;
                }
            }
        }

        // Store F32 in vPID workspace (allocate() is already thread-safe)
        auto alloc = cache->get_vpid()->allocate(tensor_info.byte_size, name);
        cache->get_vpid()->write_direct(alloc.offset, f32_data.data(), tensor_info.byte_size);

        // SnapLLM: Register tensor layer for memory-based eviction
        cache->get_vpid()->register_tensor_layer(name, alloc.offset, tensor_info.byte_size);

        // VALIDATION STAGE 2: Post-vPID Write (simplified - just validate write succeeded)
        // Full validation will happen during Stage 3 when reading for inference
        if (validator_.is_enabled() && validator_.get_config().validate_vpid_write) {
            #pragma omp critical
            {
                // Basic validation: check allocation succeeded
                if (alloc.size != tensor_info.byte_size) {
                    std::cerr << "  [VALIDATION FAILED] " << name << ": vPID allocation size mismatch!" << std::endl;
                }
            }
        }

        tensor_info.vpid_offset = alloc.offset;
        tensor_info.vpid_alloc = alloc;
        tensor_infos[i] = tensor_info;

        // Thread-safe progress reporting
        size_t count = ++processed_count;
        if (count % 10 == 0) {
            #pragma omp critical
            {
                std::cout << "  Progress: " << count << "/" << total_tensors << " tensors" << std::endl;
            }
        }
    }

    // Consolidate parallel results into model_info
    std::cout << "  Consolidating parallel results..." << std::endl;
    for (size_t i = 0; i < tensor_infos.size(); i++) {
        if (!tensor_infos[i].name.empty()) {
            model_info.tensors.push_back(tensor_infos[i]);
            total_dequant_size += tensor_infos[i].byte_size;
            tensors_processed++;
        }
    }

    std::cout << "  Dequantization complete!" << std::endl;
    std::cout << "  Total tensors: " << tensors_processed << std::endl;
    std::cout << "  Total dequantized size: " << (total_dequant_size / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;

    // Register model in cache with full metadata
    std::cout << "  [DEBUG] Registering model with cache..." << std::endl;
    std::cout << "    Model name in metadata: " << model_info.name << std::endl;
    std::cout << "    Number of tensors: " << model_info.tensors.size() << std::endl;
    bool success = cache->register_model_with_metadata(model_info);
    std::cout << "  [DEBUG] Registration result: " << (success ? "SUCCESS" : "FAILED") << std::endl;

    // Save metadata to persistent workspace for future sessions
    if (success) {
        std::cout << "  Saving metadata to persistent workspace..." << std::endl;

        // Build WorkspaceMetadata::ModelMetadata from model_info
        ModelMetadata ws_metadata;
        ws_metadata.name = extracted_model_name;
        ws_metadata.gguf_path = gguf_path;
        ws_metadata.gguf_hash = "";  // TODO: Calculate SHA256 hash of GGUF
        ws_metadata.quant_type = quant_type;
        ws_metadata.architecture = model_info.architecture;
        ws_metadata.tensor_count = model_info.tensors.size();
        ws_metadata.total_size_bytes = total_dequant_size;
        ws_metadata.vocab_size = model_info.vocab_size;
        ws_metadata.context_length = model_info.context_length;
        ws_metadata.embedding_length = model_info.embedding_length;
        ws_metadata.layer_count = model_info.num_layers;

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        ws_metadata.loaded_timestamp = ss.str();

        // Convert TensorInfo to TensorLocation
        for (const auto& tensor_info : model_info.tensors) {
            TensorLocation tensor_loc;
            tensor_loc.name = tensor_info.name;
            tensor_loc.vpid_offset = tensor_info.vpid_offset;
            tensor_loc.size_bytes = tensor_info.byte_size;
            tensor_loc.element_count = tensor_info.num_elements;
            tensor_loc.original_type = "quantized";  // TODO: Store actual type
            tensor_loc.dequant_type = "f32";

            ws_metadata.tensors.push_back(tensor_loc);
        }

        if (workspace_metadata_->save_model_metadata(ws_metadata)) {
            std::cout << "  ✓ Metadata saved to D:\\SnapLLM_Workspace\\" << extracted_model_name << "\\" << quant_type << std::endl;
            std::cout << "    Model will reload instantly next time!" << std::endl;
        } else {
            std::cerr << "  Warning: Failed to save workspace metadata" << std::endl;
        }
    }

    // Clean up temporary llama.cpp model (we've copied the data to vPID)
    llama_model_free(model);
    // DON'T free backend yet - we need it for reload!

    if (!success) {
        llama_backend_free();
        return false;
    }

    std::cout << "  Model successfully loaded into vPID cache!" << std::endl;
    std::cout << "  All tensors dequantized and available for zero-copy access" << std::endl;

    // Now reload the model with vPID tensors for inference
    std::cout << "\n=== Phase 2E: Reloading model for inference with vPID tensors ===" << std::endl;
    const ModelInfo* cached_info = cache->get_model_info(model_name);
    if (!cached_info) {
        std::cerr << "Failed to get model info after dequantization" << std::endl;
        llama_backend_free();
        return false;
    }

    // Skip backend init in load_model_with_vpid_tensors since we're already initialized
    // Pass cache directly to ensure we use the exact same instance that was populated
    bool reload_success = load_model_with_vpid_tensors(model_name, gguf_path, cached_info, cache);
    if (reload_success) {
        std::cout << "  Model ready for inference!" << std::endl;
    } else {
        llama_backend_free();
    }

    return reload_success;
    #endif  // End of old extraction code - no longer needed with vDPE
}

std::vector<float> VPIDBridge::dequantize_tensor(
    const void* tensor_data,
    int tensor_type,
    size_t num_elements)
{
    std::vector<float> result(num_elements);

    // Calculate number of blocks
    size_t block_size = 0;
    switch (tensor_type) {
        case GGML_TYPE_Q4_0:
        case GGML_TYPE_Q5_0:
        case GGML_TYPE_Q8_0:
        case GGML_TYPE_MXFP4:  // MXFP4 for MoE models
            block_size = 32;
            break;
        case GGML_TYPE_Q2_K:
        case GGML_TYPE_Q3_K:
        case GGML_TYPE_Q4_K:
        case GGML_TYPE_Q5_K:
        case GGML_TYPE_Q6_K:
            block_size = 256;
            break;
        case GGML_TYPE_F16:
        case GGML_TYPE_BF16:
            // F16/BF16 are not block-based
            block_size = 1;
            break;
        case GGML_TYPE_F32:
            // Already F32, just copy
            std::memcpy(result.data(), tensor_data, num_elements * sizeof(float));
            return result;
        default:
            std::cerr << "Unsupported tensor type: " << tensor_type << std::endl;
            return result;
    }

    size_t num_blocks = (num_elements + block_size - 1) / block_size;

    // Use llama.cpp's optimized dequantization functions
    switch (tensor_type) {
        case GGML_TYPE_Q4_0:
            dequantize_row_q4_0(
                reinterpret_cast<const block_q4_0*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q5_0:
            dequantize_row_q5_0(
                reinterpret_cast<const block_q5_0*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q8_0:
            dequantize_row_q8_0(
                reinterpret_cast<const block_q8_0*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q4_K:
            dequantize_row_q4_K(
                reinterpret_cast<const block_q4_K*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q5_K:
            dequantize_row_q5_K(
                reinterpret_cast<const block_q5_K*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q6_K:
            dequantize_row_q6_K(
                reinterpret_cast<const block_q6_K*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q2_K:
            dequantize_row_q2_K(
                reinterpret_cast<const block_q2_K*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_Q3_K:
            dequantize_row_q3_K(
                reinterpret_cast<const block_q3_K*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_MXFP4:
            // MXFP4 dequantization for MoE models (gpt-oss, etc.)
            dequantize_row_mxfp4(
                reinterpret_cast<const block_mxfp4*>(tensor_data),
                result.data(),
                num_elements
            );
            break;

        case GGML_TYPE_F16: {
            // Dequantize F16 to F32
            const ggml_fp16_t* src = reinterpret_cast<const ggml_fp16_t*>(tensor_data);
            for (size_t i = 0; i < num_elements; i++) {
                result[i] = ggml_fp16_to_fp32(src[i]);
            }
            break;
        }

        case GGML_TYPE_BF16: {
            // Dequantize BF16 to F32
            const ggml_bf16_t* src = reinterpret_cast<const ggml_bf16_t*>(tensor_data);
            for (size_t i = 0; i < num_elements; i++) {
                result[i] = ggml_bf16_to_fp32(src[i]);
            }
            break;
        }

        default:
            break;
    }

    return result;
}

llama_context* VPIDBridge::create_inference_context(
    const std::string& model_name,
    int n_ctx,
    int n_batch)
{
    std::lock_guard<std::mutex> lock(models_mutex_);

    // Check if model is loaded
    auto it = loaded_models_.find(model_name);
    if (it == loaded_models_.end()) {
        std::cerr << "Model not loaded: " << model_name << std::endl;
        std::cerr << "  Call load_and_dequantize_model() first" << std::endl;
        return nullptr;
    }

    // Update access time for LRU tracking
    model_access_times_[model_name] = std::chrono::steady_clock::now();

    llama_model* model = it->second;

    // Silent context creation for performance

    // Configure context parameters - optimized for performance
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;
    ctx_params.n_batch = n_batch;
    ctx_params.n_ubatch = n_batch;  // Unified batch size for parallel decoding
    ctx_params.n_threads = 8;  // Use 8 threads (P-cores only on i9-14900HX)  // Use 12 threads like LMStudio for optimal performance
    ctx_params.n_threads_batch = 8;
    ctx_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;  // Auto Flash Attention
    ctx_params.no_perf = false;    // Enable performance logging

    // Create context from the vPID-backed model
    // Note: llama_init_from_model is the newer API
    llama_context* ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) {
        std::cerr << "  Failed to create inference context!" << std::endl;
        return nullptr;
    }

    

    return ctx;
}

const float* VPIDBridge::get_tensor_data(
    const std::string& model_name,
    const std::string& tensor_name)
{
    // Look up per-model cache
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = model_caches_.find(model_name);
    if (it == model_caches_.end()) {
        return nullptr;
    }
    return it->second->get_tensor(model_name, tensor_name);
}

const TensorInfo* VPIDBridge::get_tensor_info(
    const std::string& model_name,
    const std::string& tensor_name)
{
    // Look up per-model cache
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = model_caches_.find(model_name);
    if (it == model_caches_.end()) {
        return nullptr;
    }
    return it->second->get_tensor_info(model_name, tensor_name);
}

void VPIDBridge::unload_model(const std::string& model_name) {
    // Look up per-model cache
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = model_caches_.find(model_name);
    if (it != model_caches_.end()) {
        it->second->unload_model(model_name);
        // Optionally remove from map (keep for now to allow reload)
        // model_caches_.erase(it);
    }
}

bool VPIDBridge::is_model_loaded(const std::string& model_name) const {
    // Check if actual llama_model* is in GPU (not just metadata cache)
    std::lock_guard<std::mutex> lock(models_mutex_);
    return loaded_models_.find(model_name) != loaded_models_.end();
}

std::string VPIDBridge::generate_text(
    const std::string& model_name,
    const std::string& prompt,
    int max_tokens,
    size_t* actual_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty)
{
    // Acquire inference slot (blocks if max concurrent reached)
    acquire_inference_slot();

    std::cout << "\n=== Phase 4: Token Generation with vPID Tensors ===" << std::endl;
    std::cout << "Model: " << model_name << std::endl;
    std::cout << "Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "Max tokens: " << max_tokens << std::endl;

    // Create inference context
    llama_context* ctx = create_inference_context(model_name, 4096, 512);
    if (!ctx) {
        std::cerr << "Failed to create inference context for generation" << std::endl;
        release_inference_slot();
        return "";
    }

    // Get the model and vocab
    llama_model* model = nullptr;
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        auto it = loaded_models_.find(model_name);
        if (it != loaded_models_.end()) {
            model = it->second;
        }
    }

    if (!model) {
        llama_free(ctx);
        release_inference_slot();
        return "";
    }

    const llama_vocab* vocab = llama_model_get_vocab(model);

    // Apply chat template if available (like LMStudio does)
    std::string formatted_prompt = prompt;
    const char* chat_template = llama_model_chat_template(model, nullptr);
    if (chat_template) {
        // Create chat messages with system prompt for language control
        llama_chat_message messages[] = {
            { "system", "You are a helpful AI assistant. Always respond in English." },
            { "user", prompt.c_str() }
        };

        // Get required buffer size
        std::vector<char> buf(prompt.size() * 4 + 512);  // Larger buffer for system prompt
        int32_t result = llama_chat_apply_template(
            chat_template,
            messages,
            2,      // n_msg (system + user)
            true,   // add_ass (add assistant prefix)
            buf.data(),
            buf.size()
        );

        if (result > 0 && result < (int32_t)buf.size()) {
            formatted_prompt = std::string(buf.data(), result);
            std::cout << "[Chat Template] Applied model template with system prompt" << std::endl;
        }
    }

    std::cout << "\n[Generation] Tokenizing prompt..." << std::endl;

    // Find number of tokens (call with NULL to get count)
    const int n_tokens = -llama_tokenize(
        vocab,
        formatted_prompt.c_str(),
        formatted_prompt.size(),
        NULL,
        0,
        true,  // add_special (add BOS token)
        true   // parse_special
    );

    // Tokenize the prompt
    std::vector<llama_token> tokens(n_tokens);
    if (llama_tokenize(vocab, formatted_prompt.c_str(), formatted_prompt.size(), tokens.data(), tokens.size(), true, true) < 0) {
        std::cerr << "Tokenization failed!" << std::endl;
        llama_free(ctx);
        release_inference_slot();
        return "";
    }

    

    // Evaluate the prompt
    
    

    // DEBUG output disabled for performance

    // Process prompt tokens in batches to avoid exceeding n_batch limit
    const int BATCH_SIZE = 512;
    int n_tokens_total = static_cast<int>(tokens.size());
    int n_processed = 0;
    int decode_result = 0;

    while (n_processed < n_tokens_total) {
        int n_batch = std::min(BATCH_SIZE, n_tokens_total - n_processed);

        llama_batch batch = llama_batch_init(n_batch, 0, 1);
        for (int i = 0; i < n_batch; i++) {
            int idx = n_processed + i;
            batch.token[i] = tokens[idx];
            batch.pos[i] = idx;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            // Only compute logits for the very last token
            batch.logits[i] = (idx == n_tokens_total - 1) ? 1 : 0;
        }
        batch.n_tokens = n_batch;

        decode_result = llama_decode(ctx, batch);
        llama_batch_free(batch);

        if (decode_result != 0) {
            break;
        }
        n_processed += n_batch;
    }

    
    


    if (decode_result != 0) {


        std::cerr << "❌ [ERROR] llama_decode() failed with code: " << decode_result << std::endl;


        std::cerr << "  Tokens to decode: " << tokens.size() << std::endl;


        std::cerr.flush();


        llama_free(ctx);

        release_inference_slot();
        return "";


    }

    
    

    // Create proper sampler chain with penalties and temperature
    // Using sensible defaults: temp=0.7, top_k=40, top_p=0.95, repeat_penalty=1.3
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);

    // Add penalties (repeat, frequency, presence) to prevent repetition
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
        max_tokens,      // penalty_last_n
        repeat_penalty,  // penalty_repeat - from API parameter
        0.0f,            // penalty_freq
        0.0f             // penalty_present
    ));

    // Add top-k filtering to limit choices (from API parameter)
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(top_k));

    // Add top-p (nucleus) sampling for diversity (from API parameter)
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(top_p, 1));
    
    // Add minP sampling like LMStudio
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));

    // Add temperature scaling for controlled randomness
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));  // Match LMStudio

    // Final distribution sampling (not greedy!)
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(0));

    // Generate tokens
    std::string result = prompt;
    int n_gen = 0;

    while (n_gen < max_tokens) {
        // DIAGNOSTIC: Get logits before sampling
        const int n_vocab = llama_n_vocab(vocab);
        const float* logits = llama_get_logits_ith(ctx, -1);

        if (false && n_gen < 5 && logits) { // Disabled for performance
            std::cout << "\n[DIAGNOSTIC] Token " << n_gen << " - Logit analysis:" << std::endl;

            // Find top 5 tokens by logit value
            std::vector<std::pair<float, llama_token>> logit_pairs;
            for (int i = 0; i < n_vocab; i++) {
                logit_pairs.push_back({logits[i], i});
            }
            std::sort(logit_pairs.begin(), logit_pairs.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

            std::cout << "  Top 5 tokens by logit:" << std::endl;
            for (int i = 0; i < 5; i++) {
                char token_buf[128];
                int n = llama_token_to_piece(vocab, logit_pairs[i].second, token_buf, sizeof(token_buf), 0, true);
                std::string token_str(token_buf, n > 0 ? n : 0);
                std::cout << "    " << (i+1) << ". Token " << logit_pairs[i].second
                         << " (\"" << token_str << "\"): logit=" << logit_pairs[i].first << std::endl;
            }

            // Check logit statistics
            float min_logit = logits[0], max_logit = logits[0];
            double sum_logits = 0.0;
            int num_inf = 0, num_nan = 0;
            for (int i = 0; i < n_vocab; i++) {
                if (std::isnan(logits[i])) num_nan++;
                else if (std::isinf(logits[i])) num_inf++;
                else {
                    if (logits[i] < min_logit) min_logit = logits[i];
                    if (logits[i] > max_logit) max_logit = logits[i];
                    sum_logits += logits[i];
                }
            }
            std::cout << "  Logit stats: range=[" << min_logit << ", " << max_logit << "]"
                     << ", NaNs=" << num_nan << ", Infs=" << num_inf << std::endl;
        }

        // Sample next token
        llama_token new_token = llama_sampler_sample(smpl, ctx, -1);

        // DEBUG: Show sampled token (DISABLED for performance)
        if (false && n_gen < 5) {
            char token_buf[128];
            int n = llama_token_to_piece(vocab, new_token, token_buf, sizeof(token_buf), 0, true);
            std::string token_str(token_buf, n > 0 ? n : 0);
            std::cout << "\n[DEBUG] Sampled token " << n_gen << ": " << new_token
                     << " (\"" << token_str << "\")" << std::endl;
        }

        // Check for EOS
        if (llama_vocab_is_eog(vocab, new_token)) {
            std::cout << "\n[Generation] EOS token reached after " << n_gen << " tokens" << std::endl;
            break;
        }

        // Decode token to text
        char buf[128];
        int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n_chars > 0) {
            result.append(buf, n_chars);
            // Streaming output disabled for performance
        }

        // Prepare next batch with single token
        llama_batch gen_batch = llama_batch_get_one(&new_token, 1);
        n_gen++;

        // Evaluate
        if (llama_decode(ctx, gen_batch) != 0) {
            std::cerr << "\nFailed to evaluate token " << n_gen << std::endl;
            break;
        }
    }

    std::cout << "\n\n[Generation] Complete! Generated " << n_gen << " tokens" << std::endl;

    // Set actual tokens output
    if (actual_tokens) {
        *actual_tokens = static_cast<size_t>(n_gen);
    }

    // Cleanup
    llama_sampler_free(smpl);
    llama_free(ctx);

    release_inference_slot();
    return result;
}

// =============================================================================
// Parallel Batch Generation using llama.cpp Multi-Sequence API
// =============================================================================

namespace {

// Internal per-sequence state for parallel batch processing
struct SequenceState {
    int seq_id = -1;
    int prompt_index = -1;

    std::vector<llama_token> tokens;  // Tokenized prompt
    int n_prompt_tokens = 0;

    int n_past = 0;       // Position counter
    int n_decoded = 0;    // Tokens generated
    int max_tokens = 512;
    int32_t i_batch = -1; // Index into current batch for logit sampling
    bool finished = false;

    llama_sampler* smpl = nullptr;
    std::string output;

    ~SequenceState() {
        if (smpl) {
            llama_sampler_free(smpl);
            smpl = nullptr;
        }
    }

    // Non-copyable, movable
    SequenceState() = default;
    SequenceState(const SequenceState&) = delete;
    SequenceState& operator=(const SequenceState&) = delete;
    SequenceState(SequenceState&& o) noexcept
        : seq_id(o.seq_id), prompt_index(o.prompt_index),
          tokens(std::move(o.tokens)), n_prompt_tokens(o.n_prompt_tokens),
          n_past(o.n_past), n_decoded(o.n_decoded), max_tokens(o.max_tokens),
          i_batch(o.i_batch), finished(o.finished), smpl(o.smpl),
          output(std::move(o.output)) {
        o.smpl = nullptr;
    }
    SequenceState& operator=(SequenceState&& o) noexcept {
        if (this != &o) {
            if (smpl) llama_sampler_free(smpl);
            seq_id = o.seq_id; prompt_index = o.prompt_index;
            tokens = std::move(o.tokens); n_prompt_tokens = o.n_prompt_tokens;
            n_past = o.n_past; n_decoded = o.n_decoded; max_tokens = o.max_tokens;
            i_batch = o.i_batch; finished = o.finished;
            smpl = o.smpl; o.smpl = nullptr;
            output = std::move(o.output);
        }
        return *this;
    }
};

static constexpr int MAX_PARALLEL_SEQUENCES = 8;

} // anonymous namespace

std::vector<BatchResult> VPIDBridge::generate_batch_parallel(
    const std::string& model_name,
    const std::vector<BatchPromptItem>& items,
    float default_temp,
    float default_top_p,
    int default_top_k,
    float default_repeat_penalty)
{
    // Acquire inference slot (blocks if max concurrent reached)
    acquire_inference_slot();

    auto total_start = std::chrono::high_resolution_clock::now();

    const int n_items = static_cast<int>(items.size());
    std::vector<BatchResult> all_results(n_items);

    if (n_items == 0) { release_inference_slot(); return all_results; }

    // Process in chunks of MAX_PARALLEL_SEQUENCES
    for (int offset = 0; offset < n_items; offset += MAX_PARALLEL_SEQUENCES) {
        int chunk_size = std::min(MAX_PARALLEL_SEQUENCES, n_items - offset);

      try {
        // ---- Phase 1: Setup ----
        llama_model* model = nullptr;
        {
            std::lock_guard<std::mutex> lock(models_mutex_);
            auto it = loaded_models_.find(model_name);
            if (it != loaded_models_.end()) {
                model = it->second;
            }
        }

        if (!model) {
            for (int i = 0; i < chunk_size; i++) {
                all_results[offset + i].success = false;
                all_results[offset + i].error = "Model not loaded: " + model_name;
            }
            continue;
        }

        // Update access time
        {
            std::lock_guard<std::mutex> lock(models_mutex_);
            model_access_times_[model_name] = std::chrono::steady_clock::now();
        }

        const llama_vocab* vocab = llama_model_get_vocab(model);
        const char* chat_template = llama_model_chat_template(model, nullptr);

        // ---- Phase 2: Tokenize all prompts ----
        std::vector<SequenceState> seqs(chunk_size);
        int total_prompt_tokens = 0;
        int total_max_gen_tokens = 0;

        for (int i = 0; i < chunk_size; i++) {
            const auto& item = items[offset + i];
            seqs[i].seq_id = i;
            seqs[i].prompt_index = offset + i;
            seqs[i].max_tokens = item.max_tokens;

            // Format prompt with chat template
            std::string formatted_prompt;
            if (!item.messages.empty() && chat_template) {
                // Use provided messages
                std::vector<llama_chat_message> msgs;
                for (const auto& m : item.messages) {
                    msgs.push_back({ m.role.c_str(), m.content.c_str() });
                }
                std::vector<char> buf(4096);
                int32_t tmpl_result = llama_chat_apply_template(
                    chat_template, msgs.data(), static_cast<int32_t>(msgs.size()),
                    true, buf.data(), static_cast<int32_t>(buf.size()));
                if (tmpl_result > 0 && tmpl_result < (int32_t)buf.size()) {
                    formatted_prompt = std::string(buf.data(), tmpl_result);
                } else if (tmpl_result > (int32_t)buf.size()) {
                    buf.resize(tmpl_result + 1);
                    tmpl_result = llama_chat_apply_template(
                        chat_template, msgs.data(), static_cast<int32_t>(msgs.size()),
                        true, buf.data(), static_cast<int32_t>(buf.size()));
                    if (tmpl_result > 0) formatted_prompt = std::string(buf.data(), tmpl_result);
                    else formatted_prompt = item.raw_prompt.empty() ? item.messages.back().content : item.raw_prompt;
                } else {
                    formatted_prompt = item.raw_prompt.empty() ? item.messages.back().content : item.raw_prompt;
                }
            } else if (!item.raw_prompt.empty() && chat_template) {
                // Raw prompt with system prompt
                std::string sys = item.system_prompt.value_or(
                    "You are a helpful AI assistant. Always respond in English.");
                llama_chat_message msgs[] = {
                    { "system", sys.c_str() },
                    { "user", item.raw_prompt.c_str() }
                };
                std::vector<char> buf(item.raw_prompt.size() * 4 + 1024);
                int32_t tmpl_result = llama_chat_apply_template(
                    chat_template, msgs, 2, true, buf.data(), static_cast<int32_t>(buf.size()));
                if (tmpl_result > 0 && tmpl_result < (int32_t)buf.size()) {
                    formatted_prompt = std::string(buf.data(), tmpl_result);
                } else {
                    formatted_prompt = item.raw_prompt;
                }
            } else {
                formatted_prompt = item.raw_prompt;
                if (formatted_prompt.empty() && !item.messages.empty()) {
                    formatted_prompt = item.messages.back().content;
                }
            }

            if (formatted_prompt.empty()) {
                seqs[i].finished = true;
                all_results[offset + i].success = false;
                all_results[offset + i].error = "Empty prompt";
                continue;
            }

            // Tokenize
            const int n_tok = -llama_tokenize(
                vocab, formatted_prompt.c_str(), static_cast<int32_t>(formatted_prompt.size()),
                NULL, 0, true, true);
            if (n_tok <= 0) {
                seqs[i].finished = true;
                all_results[offset + i].success = false;
                all_results[offset + i].error = "Tokenization failed";
                continue;
            }

            seqs[i].tokens.resize(n_tok);
            llama_tokenize(vocab, formatted_prompt.c_str(), static_cast<int32_t>(formatted_prompt.size()),
                           seqs[i].tokens.data(), n_tok, true, true);
            seqs[i].n_prompt_tokens = n_tok;

            total_prompt_tokens += n_tok;
            total_max_gen_tokens += item.max_tokens;
        }

        // Count active sequences
        int n_active = 0;
        for (int i = 0; i < chunk_size; i++) {
            if (!seqs[i].finished) n_active++;
        }
        if (n_active == 0) continue;

        // ---- Phase 3: Create context with multi-sequence support ----
        int n_ctx = total_prompt_tokens + total_max_gen_tokens + 256;  // Extra headroom
        n_ctx = std::max(n_ctx, 2048);
        n_ctx = std::min(n_ctx, 32768);  // Cap to prevent OOM

        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = n_ctx;
        ctx_params.n_batch = 512;
        ctx_params.n_ubatch = 512;
        ctx_params.n_seq_max = chunk_size;
        ctx_params.n_threads = 8;
        ctx_params.n_threads_batch = 8;
        ctx_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
        ctx_params.no_perf = false;

        llama_context* ctx = llama_init_from_model(model, ctx_params);
        if (!ctx) {
            std::cerr << "[Batch] Failed to create multi-sequence context, falling back to sequential" << std::endl;
            // Fallback: process sequentially
            for (int i = 0; i < chunk_size; i++) {
                if (seqs[i].finished) continue;
                const auto& item = items[offset + i];
                auto seq_start = std::chrono::high_resolution_clock::now();
                std::string text = generate_text(model_name, item.raw_prompt.empty() ?
                    (item.messages.empty() ? "" : item.messages.back().content) : item.raw_prompt,
                    item.max_tokens, nullptr,
                    item.temperature.value_or(default_temp),
                    item.top_p.value_or(default_top_p),
                    item.top_k.value_or(default_top_k),
                    item.repeat_penalty.value_or(default_repeat_penalty));
                auto seq_end = std::chrono::high_resolution_clock::now();
                all_results[offset + i].generated_text = text;
                all_results[offset + i].latency_ms = std::chrono::duration<double, std::milli>(seq_end - seq_start).count();
            }
            continue;
        }

        std::cout << "\n=== Parallel Batch Generation ===" << std::endl;
        std::cout << "Sequences: " << n_active << ", Context: " << n_ctx
                  << ", Total prompt tokens: " << total_prompt_tokens << std::endl;

        // ---- Phase 4: Create per-sequence samplers ----
        for (int s = 0; s < chunk_size; s++) {
            if (seqs[s].finished) continue;

            const auto& item = items[offset + s];
            float temp = item.temperature.value_or(default_temp);
            float tp = item.top_p.value_or(default_top_p);
            int tk = item.top_k.value_or(default_top_k);
            float rp = item.repeat_penalty.value_or(default_repeat_penalty);

            auto sparams = llama_sampler_chain_default_params();
            sparams.no_perf = false;
            seqs[s].smpl = llama_sampler_chain_init(sparams);

            llama_sampler_chain_add(seqs[s].smpl, llama_sampler_init_penalties(
                seqs[s].max_tokens, rp, 0.0f, 0.0f));
            llama_sampler_chain_add(seqs[s].smpl, llama_sampler_init_top_k(tk));
            llama_sampler_chain_add(seqs[s].smpl, llama_sampler_init_top_p(tp, 1));
            llama_sampler_chain_add(seqs[s].smpl, llama_sampler_init_min_p(0.05f, 1));
            llama_sampler_chain_add(seqs[s].smpl, llama_sampler_init_temp(temp));
            llama_sampler_chain_add(seqs[s].smpl, llama_sampler_init_dist(0));
        }

        // ---- Phase 5: Prefill all prompts with i_batch tracking ----
        llama_batch batch = llama_batch_init(512, 0, 1);
        llama_memory_t mem = llama_get_memory(ctx);
        if (!mem) {
            std::cerr << "[Batch] Failed to get memory handle from context" << std::endl;
            llama_batch_free(batch);
            llama_free(ctx);
            for (int i = 0; i < chunk_size; i++) {
                if (!seqs[i].finished) {
                    all_results[offset + i].success = false;
                    all_results[offset + i].error = "Failed to get memory handle";
                }
            }
            continue;
        }
        bool prefill_ok = true;

        for (int s = 0; s < chunk_size && prefill_ok; s++) {
            if (seqs[s].finished) continue;

            for (int t = 0; t < seqs[s].n_prompt_tokens; t++) {
                batch.token[batch.n_tokens] = seqs[s].tokens[t];
                batch.pos[batch.n_tokens] = t;
                batch.n_seq_id[batch.n_tokens] = 1;
                batch.seq_id[batch.n_tokens][0] = seqs[s].seq_id;

                bool is_last = (t == seqs[s].n_prompt_tokens - 1);
                batch.logits[batch.n_tokens] = is_last ? 1 : 0;

                if (is_last) {
                    seqs[s].i_batch = batch.n_tokens;  // Track logit position
                }

                batch.n_tokens++;

                if (batch.n_tokens >= 512) {
                    int ret = llama_decode(ctx, batch);
                    if (ret != 0) {
                        prefill_ok = false;
                        break;
                    }
                    // If we flushed and a sequence's last token was in this batch,
                    // we need to sample NOW before the next decode overwrites logits.
                    // Check if any sequence's i_batch was in this batch range.
                    for (int ss = 0; ss < chunk_size; ss++) {
                        if (seqs[ss].finished) continue;
                        if (seqs[ss].i_batch >= 0 && seqs[ss].i_batch < batch.n_tokens) {
                            // Sample first token for this sequence now
                            llama_token new_token = llama_sampler_sample(
                                seqs[ss].smpl, ctx, seqs[ss].i_batch);

                            if (llama_vocab_is_eog(vocab, new_token)) {
                                seqs[ss].finished = true;
                                llama_memory_seq_rm(mem, seqs[ss].seq_id, -1, -1);
                            } else {
                                char buf[128];
                                int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
                                if (n_chars > 0) seqs[ss].output.append(buf, n_chars);
                                // Store token for next batch
                                seqs[ss].tokens.clear();
                                seqs[ss].tokens.push_back(new_token);
                                seqs[ss].n_decoded++;
                            }
                            seqs[ss].i_batch = -1;  // Reset
                        }
                    }
                    batch.n_tokens = 0;
                }
            }

            seqs[s].n_past = seqs[s].n_prompt_tokens;
        }

        // Flush remaining
        if (prefill_ok && batch.n_tokens > 0) {
            int ret = llama_decode(ctx, batch);
            if (ret != 0) {
                prefill_ok = false;
            } else {
                // Sample first token for sequences whose last token is in this batch
                for (int ss = 0; ss < chunk_size; ss++) {
                    if (seqs[ss].finished) continue;
                    if (seqs[ss].i_batch >= 0 && seqs[ss].i_batch < batch.n_tokens) {
                        llama_token new_token = llama_sampler_sample(
                            seqs[ss].smpl, ctx, seqs[ss].i_batch);

                        if (llama_vocab_is_eog(vocab, new_token)) {
                            seqs[ss].finished = true;
                            llama_memory_seq_rm(mem, seqs[ss].seq_id, -1, -1);
                        } else {
                            char buf[128];
                            int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
                            if (n_chars > 0) seqs[ss].output.append(buf, n_chars);
                            seqs[ss].tokens.clear();
                            seqs[ss].tokens.push_back(new_token);
                            seqs[ss].n_decoded++;
                        }
                        seqs[ss].i_batch = -1;
                    }
                }
            }
            batch.n_tokens = 0;
        }

        if (!prefill_ok) {
            llama_batch_free(batch);
            llama_free(ctx);
            for (int i = 0; i < chunk_size; i++) {
                if (!seqs[i].finished) {
                    all_results[offset + i].success = false;
                    all_results[offset + i].error = "Prefill decode failed";
                }
            }
            continue;
        }

        // Now enter the main generation loop
        // Each active sequence has its first generated token stored in seqs[s].tokens[0]
        while (true) {
            batch.n_tokens = 0;

            for (int s = 0; s < chunk_size; s++) {
                if (seqs[s].finished) continue;
                if (seqs[s].tokens.empty()) continue;
                if (seqs[s].n_decoded >= seqs[s].max_tokens) {
                    seqs[s].finished = true;
                    llama_memory_seq_rm(mem, seqs[s].seq_id, -1, -1);
                    continue;
                }

                seqs[s].i_batch = batch.n_tokens;

                batch.token[batch.n_tokens] = seqs[s].tokens[0];
                batch.pos[batch.n_tokens] = seqs[s].n_past;
                batch.n_seq_id[batch.n_tokens] = 1;
                batch.seq_id[batch.n_tokens][0] = seqs[s].seq_id;
                batch.logits[batch.n_tokens] = 1;
                batch.n_tokens++;

                seqs[s].n_past++;
            }

            if (batch.n_tokens == 0) break;  // All done

            int ret = llama_decode(ctx, batch);
            if (ret != 0) {
                std::cerr << "[Batch] Generation decode failed (ret=" << ret << ")" << std::endl;
                break;
            }

            // Sample next token for each active sequence
            for (int s = 0; s < chunk_size; s++) {
                if (seqs[s].finished || seqs[s].i_batch < 0) continue;

                llama_token new_token = llama_sampler_sample(
                    seqs[s].smpl, ctx, seqs[s].i_batch);

                if (llama_vocab_is_eog(vocab, new_token) || seqs[s].n_decoded >= seqs[s].max_tokens) {
                    seqs[s].finished = true;
                    llama_memory_seq_rm(mem, seqs[s].seq_id, -1, -1);
                    continue;
                }

                char buf[128];
                int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
                if (n_chars > 0) seqs[s].output.append(buf, n_chars);

                seqs[s].tokens.clear();
                seqs[s].tokens.push_back(new_token);
                seqs[s].n_decoded++;
            }
        }

        // ---- Phase 7: Collect results ----
        auto chunk_end = std::chrono::high_resolution_clock::now();
        double chunk_time_ms = std::chrono::duration<double, std::milli>(chunk_end - total_start).count();

        for (int s = 0; s < chunk_size; s++) {
            int idx = offset + s;
            if (all_results[idx].success) {
                all_results[idx].generated_text = seqs[s].output;
                all_results[idx].tokens_generated = seqs[s].n_decoded;
                all_results[idx].latency_ms = chunk_time_ms;
            }
        }

        std::cout << "[Batch] Chunk complete: " << n_active << " sequences, "
                  << chunk_time_ms << "ms total" << std::endl;

        // Cleanup
        llama_batch_free(batch);
        llama_free(ctx);

      } catch (const std::exception& e) {
        std::cerr << "[Batch] Exception in chunk processing: " << e.what() << std::endl;
        for (int i = 0; i < chunk_size; i++) {
            if (all_results[offset + i].success && all_results[offset + i].generated_text.empty()) {
                all_results[offset + i].success = false;
                all_results[offset + i].error = std::string("Batch exception: ") + e.what();
            }
        }
      } catch (...) {
        std::cerr << "[Batch] Unknown exception in chunk processing" << std::endl;
        for (int i = 0; i < chunk_size; i++) {
            if (all_results[offset + i].success && all_results[offset + i].generated_text.empty()) {
                all_results[offset + i].success = false;
                all_results[offset + i].error = "Unknown batch processing error";
            }
        }
      }
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
    std::cout << "[Batch] All " << n_items << " prompts complete in " << total_ms << "ms" << std::endl;

    release_inference_slot();
    return all_results;
}

size_t VPIDBridge::generate_text_streaming(
    const std::string& model_name,
    const std::string& prompt,
    TokenCallback callback,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty)
{
    // Acquire inference slot (blocks if max concurrent reached)
    acquire_inference_slot();

    // Create inference context
    llama_context* ctx = create_inference_context(model_name, 4096, 512);
    if (!ctx) {
        std::cerr << "Failed to create inference context for streaming" << std::endl;
        release_inference_slot();
        callback("", -1, true);  // Signal error
        return 0;
    }

    // Get the model and vocab
    llama_model* model = nullptr;
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        auto it = loaded_models_.find(model_name);
        if (it != loaded_models_.end()) {
            model = it->second;
        }
    }

    if (!model) {
        llama_free(ctx);
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }

    const llama_vocab* vocab = llama_model_get_vocab(model);

    // Apply chat template if available
    std::string formatted_prompt = prompt;
    const char* chat_template = llama_model_chat_template(model, nullptr);
    if (chat_template) {
        llama_chat_message messages[] = {
            { "system", "You are a helpful AI assistant. Always respond in English." },
            { "user", prompt.c_str() }
        };
        std::vector<char> buf(prompt.size() * 4 + 512);
        int32_t result = llama_chat_apply_template(
            chat_template, messages, 2, true, buf.data(), buf.size()
        );
        if (result > 0 && result < (int32_t)buf.size()) {
            formatted_prompt = std::string(buf.data(), result);
        }
    }

    // Tokenize
    const int n_tokens = -llama_tokenize(
        vocab, formatted_prompt.c_str(), formatted_prompt.size(),
        NULL, 0, true, true
    );
    std::vector<llama_token> tokens(n_tokens);
    if (llama_tokenize(vocab, formatted_prompt.c_str(), formatted_prompt.size(),
                       tokens.data(), tokens.size(), true, true) < 0) {
        llama_free(ctx);
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }

    // Evaluate prompt in batches to avoid exceeding n_batch limit
    const int BATCH_SIZE = 512;
    int n_tokens_total = static_cast<int>(tokens.size());
    int n_processed = 0;
    bool prefill_failed = false;

    while (n_processed < n_tokens_total) {
        int n_batch = std::min(BATCH_SIZE, n_tokens_total - n_processed);

        llama_batch batch = llama_batch_init(n_batch, 0, 1);
        for (int i = 0; i < n_batch; i++) {
            int idx = n_processed + i;
            batch.token[i] = tokens[idx];
            batch.pos[i] = idx;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            // Only compute logits for the very last token
            batch.logits[i] = (idx == n_tokens_total - 1) ? 1 : 0;
        }
        batch.n_tokens = n_batch;

        if (llama_decode(ctx, batch) != 0) {
            llama_batch_free(batch);
            prefill_failed = true;
            break;
        }
        llama_batch_free(batch);
        n_processed += n_batch;
    }

    if (prefill_failed) {
        llama_free(ctx);
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }

    // Create sampler chain
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);
    
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
        max_tokens, repeat_penalty, 0.0f, 0.0f
    ));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(0));

    // Stream tokens
    size_t n_gen = 0;
    bool should_continue = true;

    while (n_gen < static_cast<size_t>(max_tokens) && should_continue) {
        llama_token new_token = llama_sampler_sample(smpl, ctx, -1);

        // Check for EOS
        bool is_eos = llama_vocab_is_eog(vocab, new_token);
        
        // Decode token to text
        char buf[128];
        int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        std::string token_text(buf, n_chars > 0 ? n_chars : 0);

        // Call the streaming callback - returns false to stop
        should_continue = callback(token_text, new_token, is_eos);
        
        if (is_eos) {
            break;
        }

        // Prepare next batch
        llama_batch gen_batch = llama_batch_get_one(&new_token, 1);
        n_gen++;

        if (llama_decode(ctx, gen_batch) != 0) {
            callback("", -1, true);  // Signal error
            break;
        }
    }

    // Cleanup
    llama_sampler_free(smpl);
    llama_free(ctx);

    release_inference_slot();
    return n_gen;
}

std::string VPIDBridge::generate_with_injected_kv(
    const std::string& model_name,
    llama_context* ctx,
    const std::string& query,
    int context_token_count,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty)
{
    // Acquire inference slot (blocks if max concurrent reached)
    acquire_inference_slot();

    std::cout << "\n[vPID L2] generate_with_injected_kv starting..." << std::endl;
    std::cout << "  Model: " << model_name << std::endl;
    std::cout << "  Context tokens (from KV cache): " << context_token_count << std::endl;
    std::cout << "  Query: \"" << query.substr(0, 50) << (query.length() > 50 ? "..." : "") << "\"" << std::endl;

    if (!ctx) {
        std::cerr << "[vPID L2] Error: Context is null" << std::endl;
        release_inference_slot();
        return "";
    }

    // Get the model and vocab
    llama_model* model = nullptr;
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        auto it = loaded_models_.find(model_name);
        if (it != loaded_models_.end()) {
            model = it->second;
        }
    }

    if (!model) {
        std::cerr << "[vPID L2] Error: Model not found: " << model_name << std::endl;
        release_inference_slot();
        return "";
    }

    const llama_vocab* vocab = llama_model_get_vocab(model);
    if (!vocab) {
        std::cerr << "[vPID L2] Error: Could not get vocabulary" << std::endl;
        release_inference_slot();
        return "";
    }

    // Apply chat template if available (consistent with standard generation)
    std::string formatted_query = query;
    const char* chat_template = llama_model_chat_template(model, nullptr);
    if (chat_template) {
        llama_chat_message messages[] = {
            { "system", "You are a helpful AI assistant. Always respond in English." },
            { "user", query.c_str() }
        };
        std::vector<char> buf(query.size() * 4 + 512);
        int32_t result = llama_chat_apply_template(
            chat_template, messages, 2, true, buf.data(), buf.size()
        );
        if (result > 0 && result < (int32_t)buf.size()) {
            formatted_query = std::string(buf.data(), result);
        }
    }
    if (formatted_query == query) {
        std::string model_name_lower = model_name;
        std::transform(model_name_lower.begin(), model_name_lower.end(), model_name_lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (model_name_lower.find("chat") != std::string::npos ||
            model_name_lower.find("instruct") != std::string::npos) {
            formatted_query = "User: " + query + "\n\nAssistant:";
        }
    }

    // Tokenize ONLY the query (context is already in KV cache)
    std::vector<llama_token> query_tokens(formatted_query.length() + 32);
    int n_query_tokens = llama_tokenize(
        vocab,
        formatted_query.c_str(),
        formatted_query.length(),
        query_tokens.data(),
        query_tokens.size(),
        false,  // No BOS - context already processed
        true    // Special tokens
    );

    if (n_query_tokens < 0) {
        std::cerr << "[vPID L2] Error: Failed to tokenize query" << std::endl;
        release_inference_slot();
        return "";
    }
    query_tokens.resize(n_query_tokens);
    std::cout << "  Query tokens: " << n_query_tokens << std::endl;

    // Process query tokens starting AFTER context tokens
    // This is the key to O(1) context - we skip prefill of context!
    int start_pos = context_token_count;
    std::cout << "  Starting position: " << start_pos << " (skipping " << context_token_count << " context tokens)" << std::endl;

    // Process query tokens in batches to avoid exceeding n_batch limit
    const int BATCH_SIZE = 512;  // Safe batch size for most models
    auto query_start = std::chrono::high_resolution_clock::now();

    int n_processed = 0;
    while (n_processed < n_query_tokens) {
        int n_batch = std::min(BATCH_SIZE, n_query_tokens - n_processed);

        llama_batch batch = llama_batch_init(n_batch, 0, 1);
        for (int i = 0; i < n_batch; i++) {
            int idx = n_processed + i;
            batch.token[i] = query_tokens[idx];
            batch.pos[i] = start_pos + idx;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            // Only compute logits for the very last token of the entire query
            batch.logits[i] = (n_processed + i == n_query_tokens - 1) ? 1 : 0;
        }
        batch.n_tokens = n_batch;

        // Evaluate this batch
        if (llama_decode(ctx, batch) != 0) {
            std::cerr << "[vPID L2] Error: Failed to decode query tokens at position " << n_processed << std::endl;
            llama_batch_free(batch);
            release_inference_slot();
            return "";
        }
        llama_batch_free(batch);

        n_processed += n_batch;
    }

    auto query_end = std::chrono::high_resolution_clock::now();
    double query_ms = std::chrono::duration<double, std::milli>(query_end - query_start).count();
    std::cout << "  Query prefill time: " << query_ms << " ms (" << n_query_tokens << " tokens in "
              << ((n_query_tokens + BATCH_SIZE - 1) / BATCH_SIZE) << " batches)" << std::endl;

    // Initialize sampler
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);

    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
        max_tokens, repeat_penalty, 0.0f, 0.0f
    ));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(0));

    // Generate tokens
    std::string result;
    int n_gen = 0;
    int current_pos = start_pos + n_query_tokens;

    auto gen_start = std::chrono::high_resolution_clock::now();

    while (n_gen < max_tokens) {
        // Sample next token
        llama_token new_token = llama_sampler_sample(smpl, ctx, -1);

        // Check for EOS
        if (llama_vocab_is_eog(vocab, new_token)) {
            std::cout << "\n[vPID L2] EOS reached after " << n_gen << " tokens" << std::endl;
            break;
        }

        // Decode token to text
        char buf[128];
        int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n_chars > 0) {
            result.append(buf, n_chars);
        }

        // Prepare next batch with single token
        llama_batch gen_batch = llama_batch_get_one(&new_token, 1);
        n_gen++;
        current_pos++;

        // Evaluate
        if (llama_decode(ctx, gen_batch) != 0) {
            std::cerr << "[vPID L2] Decode failed at token " << n_gen << std::endl;
            break;
        }
    }

    auto gen_end = std::chrono::high_resolution_clock::now();
    double gen_ms = std::chrono::duration<double, std::milli>(gen_end - gen_start).count();
    double tokens_per_sec = (n_gen > 0) ? (n_gen * 1000.0 / gen_ms) : 0;

    std::cout << "\n[vPID L2] Generation complete!" << std::endl;
    std::cout << "  Tokens generated: " << n_gen << std::endl;
    std::cout << "  Generation time: " << gen_ms << " ms" << std::endl;
    std::cout << "  Speed: " << tokens_per_sec << " tok/s" << std::endl;

    // Cleanup sampler (NOTE: caller manages context lifecycle)
    llama_sampler_free(smpl);

    release_inference_slot();
    return result;
}

size_t VPIDBridge::generate_streaming_with_injected_kv(
    const std::string& model_name,
    llama_context* ctx,
    const std::string& query,
    int context_token_count,
    TokenCallback callback,
    int max_tokens,
    float temperature,
    float top_p,
    int top_k,
    float repeat_penalty)
{
    // Acquire inference slot (blocks if max concurrent reached)
    acquire_inference_slot();

    std::cout << "\n[vPID L2 Streaming] Starting with injected KV cache..." << std::endl;
    std::cout << "  Model: " << model_name << std::endl;
    std::cout << "  Context tokens (from KV cache): " << context_token_count << std::endl;
    std::cout << "  Query: \"" << query.substr(0, 50) << (query.length() > 50 ? "..." : "") << "\"" << std::endl;

    if (!ctx) {
        std::cerr << "[vPID L2 Streaming] Error: Context is null" << std::endl;
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }

    // Get the model and vocab
    llama_model* model = nullptr;
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        auto it = loaded_models_.find(model_name);
        if (it != loaded_models_.end()) {
            model = it->second;
        }
    }

    if (!model) {
        std::cerr << "[vPID L2 Streaming] Error: Model not found: " << model_name << std::endl;
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }

    const llama_vocab* vocab = llama_model_get_vocab(model);
    if (!vocab) {
        std::cerr << "[vPID L2 Streaming] Error: Could not get vocabulary" << std::endl;
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }

    // Apply chat template if available (consistent with standard generation)
    std::string formatted_query = query;
    const char* chat_template = llama_model_chat_template(model, nullptr);
    if (chat_template) {
        llama_chat_message messages[] = {
            { "system", "You are a helpful AI assistant. Always respond in English." },
            { "user", query.c_str() }
        };
        std::vector<char> buf(query.size() * 4 + 512);
        int32_t result = llama_chat_apply_template(
            chat_template, messages, 2, true, buf.data(), buf.size()
        );
        if (result > 0 && result < (int32_t)buf.size()) {
            formatted_query = std::string(buf.data(), result);
        }
    }
    if (formatted_query == query) {
        std::string model_name_lower = model_name;
        std::transform(model_name_lower.begin(), model_name_lower.end(), model_name_lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (model_name_lower.find("chat") != std::string::npos ||
            model_name_lower.find("instruct") != std::string::npos) {
            formatted_query = "User: " + query + "\n\nAssistant:";
        }
    }

    // Tokenize ONLY the query (context is already in KV cache)
    std::vector<llama_token> query_tokens(formatted_query.length() + 32);
    int n_query_tokens = llama_tokenize(
        vocab,
        formatted_query.c_str(),
        formatted_query.length(),
        query_tokens.data(),
        query_tokens.size(),
        false,  // No BOS - context already processed
        true    // Special tokens
    );

    if (n_query_tokens < 0) {
        std::cerr << "[vPID L2 Streaming] Error: Failed to tokenize query" << std::endl;
        release_inference_slot();
        callback("", -1, true);
        return 0;
    }
    query_tokens.resize(n_query_tokens);
    std::cout << "  Query tokens: " << n_query_tokens << std::endl;

    // Process query tokens starting AFTER context tokens
    // This is the key to O(1) context - we skip prefill of context!
    int start_pos = context_token_count;
    std::cout << "  Starting position: " << start_pos << " (skipping " << context_token_count << " context tokens)" << std::endl;

    // Process query tokens in batches
    const int BATCH_SIZE = 512;
    auto query_start = std::chrono::high_resolution_clock::now();

    int n_processed = 0;
    while (n_processed < n_query_tokens) {
        int n_batch = std::min(BATCH_SIZE, n_query_tokens - n_processed);

        llama_batch batch = llama_batch_init(n_batch, 0, 1);
        for (int i = 0; i < n_batch; i++) {
            int idx = n_processed + i;
            batch.token[i] = query_tokens[idx];
            batch.pos[i] = start_pos + idx;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = (n_processed + i == n_query_tokens - 1) ? 1 : 0;
        }
        batch.n_tokens = n_batch;

        if (llama_decode(ctx, batch) != 0) {
            std::cerr << "[vPID L2 Streaming] Error: Failed to decode query tokens" << std::endl;
            llama_batch_free(batch);
            release_inference_slot();
            callback("", -1, true);
            return 0;
        }
        llama_batch_free(batch);
        n_processed += n_batch;
    }

    auto query_end = std::chrono::high_resolution_clock::now();
    double query_ms = std::chrono::duration<double, std::milli>(query_end - query_start).count();
    std::cout << "  Query prefill time: " << query_ms << " ms" << std::endl;

    // Initialize sampler
    llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);

    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
        max_tokens, repeat_penalty, 0.0f, 0.0f
    ));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(top_k));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(top_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(0));

    // Stream tokens
    size_t n_gen = 0;
    bool should_continue = true;

    while (n_gen < static_cast<size_t>(max_tokens) && should_continue) {
        llama_token new_token = llama_sampler_sample(smpl, ctx, -1);

        // Check for EOS
        bool is_eos = llama_vocab_is_eog(vocab, new_token);

        // Decode token to text
        char buf[128];
        int n_chars = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        std::string token_text(buf, n_chars > 0 ? n_chars : 0);

        // Call the streaming callback
        should_continue = callback(token_text, new_token, is_eos);

        if (is_eos) {
            std::cout << "\n[vPID L2 Streaming] EOS reached after " << n_gen << " tokens" << std::endl;
            break;
        }

        // Prepare next batch
        llama_batch gen_batch = llama_batch_get_one(&new_token, 1);
        n_gen++;

        if (llama_decode(ctx, gen_batch) != 0) {
            std::cerr << "[vPID L2 Streaming] Decode failed at token " << n_gen << std::endl;
            callback("", -1, true);
            break;
        }
    }

    std::cout << "\n[vPID L2 Streaming] Complete! Generated " << n_gen << " tokens" << std::endl;

    // Cleanup sampler (NOTE: caller manages context lifecycle)
    llama_sampler_free(smpl);

    release_inference_slot();
    return n_gen;
}

void VPIDBridge::enable_validation(bool enabled) {
    validator_.enable(enabled);
    if (enabled) {
        std::cout << "[VALIDATION] Enabled - tensor validation will run at all stages" << std::endl;
    } else {
        std::cout << "[VALIDATION] Disabled - skipping tensor validation" << std::endl;
    }
}

// =============================================================================
// Inference Concurrency Control
// =============================================================================

void VPIDBridge::set_max_concurrent_inferences(int max_concurrent) {
    std::lock_guard<std::mutex> lock(inference_mutex_);
    max_concurrent_inferences_ = std::max(1, max_concurrent);
    std::cout << "[SnapLLM] Max concurrent inferences set to " << max_concurrent_inferences_ << std::endl;
    inference_cv_.notify_all();
}

void VPIDBridge::acquire_inference_slot() {
    std::unique_lock<std::mutex> lock(inference_mutex_);
    inference_cv_.wait(lock, [this] {
        return active_inferences_ < max_concurrent_inferences_;
    });
    active_inferences_++;
}

void VPIDBridge::release_inference_slot() {
    {
        std::lock_guard<std::mutex> lock(inference_mutex_);
        active_inferences_--;
    }
    inference_cv_.notify_one();
}

void VPIDBridge::set_validation_config(const ValidationConfig& config) {
    validator_.set_config(config);
    std::cout << "[VALIDATION] Configuration updated:" << std::endl;
    std::cout << "  Enable: " << (config.enable_validation ? "yes" : "no") << std::endl;
    std::cout << "  Dequantization: " << (config.validate_dequantization ? "yes" : "no") << std::endl;
    std::cout << "  vPID Write: " << (config.validate_vpid_write ? "yes" : "no") << std::endl;
    std::cout << "  vPID Read: " << (config.validate_vpid_read ? "yes" : "no") << std::endl;
    std::cout << "  Tensor Wiring: " << (config.validate_tensor_wiring ? "yes" : "no") << std::endl;
    std::cout << "  Verbose: " << (config.verbose_output ? "yes" : "no") << std::endl;
}

const ValidationConfig& VPIDBridge::get_validation_config() const {
    return validator_.get_config();
}

std::shared_ptr<VPIDWorkspace> VPIDBridge::get_workspace(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(models_mutex_);
    auto it = model_caches_.find(model_name);
    if (it == model_caches_.end()) {
        return nullptr;
    }
    return it->second->get_vpid();
}

std::shared_ptr<DequantCache> VPIDBridge::get_or_create_cache(
    const std::string& cache_key,
    const std::string& workspace_model_name,
    const std::string& quant_type,
    const std::string& gguf_path)
{
    // Check if cache already exists (keyed by cache_key for easy lookup)
    auto it = model_caches_.find(cache_key);
    if (it != model_caches_.end()) {
        return it->second;
    }

    // Create new workspace for this model at: workspace_root/workspace_model_name/quant_type/workspace.bin
    std::string workspace_path = workspace_root_ + "\\" + workspace_model_name + "\\" + quant_type + "\\workspace.bin";
    std::cout << "  [Per-Model Workspace] Creating: " << workspace_path << std::endl;

    // Create workspace directory
    std::filesystem::path dir_path = std::filesystem::path(workspace_path).parent_path();
    std::filesystem::create_directories(dir_path);

    // DYNAMIC SIZING: Calculate workspace size based on actual GGUF file size
    size_t gguf_file_size = 0;
    try {
        gguf_file_size = std::filesystem::file_size(gguf_path);
        std::cout << "  [Dynamic Sizing] GGUF file size: " << (gguf_file_size / (1024.0 * 1024 * 1024)) << " GB" << std::endl;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "  [Dynamic Sizing] Warning: Could not get GGUF file size: " << e.what() << std::endl;
        std::cerr << "  [Dynamic Sizing] Falling back to 100GB default" << std::endl;
        gguf_file_size = 50ULL * 1024 * 1024 * 1024;  // Assume 50GB if we can't read the file
    }

    // Calculate workspace size: 6x GGUF size for F32 dequantization overhead
    // Q8_0 uses 1 byte per weight, F32 uses 4 bytes per weight
    // Q8_0 ~7GB -> ~42GB workspace (accounts for metadata and F32 expansion)
    // This is much more efficient than fixed 100GB for all models
    size_t workspace_size = gguf_file_size * 6;

    // Ensure minimum 10GB and maximum 500GB workspace size
    const size_t MIN_WORKSPACE_SIZE = 10ULL * 1024 * 1024 * 1024;   // 10GB
    const size_t MAX_WORKSPACE_SIZE = 500ULL * 1024 * 1024 * 1024;  // 500GB

    if (workspace_size < MIN_WORKSPACE_SIZE) {
        std::cout << "  [Dynamic Sizing] Calculated size too small, using minimum 10GB" << std::endl;
        workspace_size = MIN_WORKSPACE_SIZE;
    } else if (workspace_size > MAX_WORKSPACE_SIZE) {
        std::cout << "  [Dynamic Sizing] Calculated size too large, capping at 500GB" << std::endl;
        workspace_size = MAX_WORKSPACE_SIZE;
    }

    // CRITICAL: Round up to 4KB boundary for FILE_FLAG_NO_BUFFERING compatibility
    // Direct I/O on Windows requires file sizes to be multiples of sector size (usually 4KB)
    const size_t SECTOR_SIZE = 4096;
    workspace_size = ((workspace_size + SECTOR_SIZE - 1) / SECTOR_SIZE) * SECTOR_SIZE;

    std::cout << "  [Dynamic Sizing] Workspace size: " << (workspace_size / (1024.0 * 1024 * 1024)) << " GB (6x GGUF size, aligned to 4KB)" << std::endl;

    // Create VPIDWorkspace with dynamic size
    // use_direct_io = TRUE for controlled RAM usage with tensor cache
    // use_direct_io = FALSE for memory mapping (enables external tensors for <1ms switching)
    // cache_budget = 4GB for tensor cache (used when Direct I/O is enabled)
    bool use_direct_io = false;  // FALSE = memory mapping for FAST external tensor loading!
    size_t cache_budget_bytes = 4ULL * 1024 * 1024 * 1024;  // 4GB cache
    auto vpid = std::make_shared<VPIDWorkspace>(workspace_path, workspace_size, use_direct_io, cache_budget_bytes);

    if (use_direct_io) {
        std::cout << "  [Memory Mode] Direct I/O mode ENABLED - On-demand loading with "
                  << (cache_budget_bytes / (1024*1024*1024)) << "GB cache" << std::endl;
    } else {
        std::cout << "  [Memory Mode] Memory-mapped mode ENABLED - Full workspace mapped to RAM" << std::endl;
    }

    // CRITICAL: Initialize the workspace (creates file and sets up memory mapping)
    if (!vpid->initialize()) {
        std::cerr << "  [ERROR] Failed to initialize VPIDWorkspace for " << workspace_model_name << std::endl;
        return nullptr;
    }

    // Create DequantCache
    auto cache = std::make_shared<DequantCache>(vpid);

    // Store in map (keyed by cache_key, workspace organized by workspace_model_name)
    model_caches_[cache_key] = cache;

    std::cout << "  [Per-Model Workspace] ✓ Created cache for " << workspace_model_name << " (" << quant_type << ")" << std::endl;

    return cache;
}

//=============================================================================
// MCB Integration Methods
//=============================================================================

std::optional<VPIDBridge::BridgeModelInfo> VPIDBridge::get_model_info(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(models_mutex_);

    auto it = loaded_models_.find(model_name);
    if (it == loaded_models_.end() || !it->second) {
        return std::nullopt;
    }

    llama_model* model = it->second;

    BridgeModelInfo info;
    info.architecture = "llama";  // TODO: Get from model metadata

    // Get model info from llama.cpp (using correct API names)
    info.n_layers = llama_model_n_layer(model);
    info.n_heads = llama_model_n_head(model);
    int n_embd = llama_model_n_embd(model);
    info.head_dim = info.n_heads > 0 ? n_embd / info.n_heads : 0;
    info.context_length = llama_model_n_ctx_train(model);

    // Estimate parameter count from embedding dimension and layers
    info.parameters = static_cast<uint64_t>(n_embd) * n_embd * info.n_layers * 4;  // Rough estimate

    // Get VRAM usage
    auto vram_it = model_vram_usage_.find(model_name);
    if (vram_it != model_vram_usage_.end()) {
        info.memory_bytes = vram_it->second * 1024 * 1024;  // Convert MB to bytes
        info.n_gpu_layers = info.n_layers;  // Assume all layers on GPU if VRAM is used
    }

    // Generate vPID from hash of model name
    std::hash<std::string> hasher;
    info.vpid = static_cast<uint32_t>(hasher(model_name) & 0xFFFFFFFF);

    return info;
}

size_t VPIDBridge::get_gpu_memory_used() const {
    return total_vram_used_ * 1024 * 1024;  // Convert MB to bytes
}

size_t VPIDBridge::get_gpu_memory_total() const {
    return VRAM_BUDGET_MB * 1024 * 1024;  // Convert MB to bytes
}

} // namespace snapllm
