#include "llama-vdpe-layer-manager.h"
#include "llama-impl.h"
#include "ggml.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <glob.h>
    #include <sys/mman.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

// Forward declare VPIDWorkspace read_direct method
namespace snapllm {
    class VPIDWorkspace {
    public:
        template<typename T>
        const T* read_direct(const char* file_path, size_t count);
    };
}

namespace vdpe {

// ============================================================================
// Constructor / Destructor
// ============================================================================

VDPELayerManager::VDPELayerManager(snapllm::VPIDWorkspace* workspace,
                                   const std::string& cache_dir,
                                   const VDPEConfig& config)
    : workspace_(workspace),
      cache_dir_(cache_dir),
      config_(config),
      num_layers_(0),
      total_wired_bytes_(0) {

    LLAMA_LOG_INFO("%s: Initializing VDPELayerManager\n", __func__);
    LLAMA_LOG_INFO("%s:   - Cache dir: %s\n", __func__, cache_dir_.c_str());
    LLAMA_LOG_INFO("%s:   - Max initial layers: %d\n", __func__, config_.max_initial_layers);
    LLAMA_LOG_INFO("%s:   - RAM budget: %zu GB\n", __func__, config_.max_ram_budget_gb);
    LLAMA_LOG_INFO("%s:   - Eviction enabled: %s\n", __func__, config_.enable_eviction ? "yes" : "no");
}

VDPELayerManager::~VDPELayerManager() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Unmap all tensors
    for (auto& [name, info] : tensors_) {
        if (info.mapped_ptr != nullptr) {
            // Unmapping is handled by workspace
            info.mapped_ptr = nullptr;
        }
    }

    LLAMA_LOG_INFO("%s: VDPELayerManager destroyed\n", __func__);
}

// ============================================================================
// Layer State Building
// ============================================================================

int VDPELayerManager::build_layer_state_from_cache() {
    std::lock_guard<std::mutex> lock(mutex_);

    LLAMA_LOG_INFO("%s: Building layer state from cache directory...\n", __func__);

    #ifdef _WIN32
        // Windows: Use FindFirstFile/FindNextFile
        WIN32_FIND_DATAA find_data;
        std::string search_path = cache_dir_ + "/*.f32";
        HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);

        if (hFind == INVALID_HANDLE_VALUE) {
            LLAMA_LOG_WARN("%s: No .f32 files found in cache directory\n", __func__);
            return 0;
        }

        do {
            std::string filename = find_data.cFileName;
            if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".f32") {
                std::string tensor_name = filename.substr(0, filename.length() - 4);
                int layer_num = extract_layer_num(tensor_name);

                if (layer_num >= 0 && layer_num >= num_layers_) {
                    num_layers_ = layer_num + 1;
                }
            }
        } while (FindNextFileA(hFind, &find_data) != 0);

        FindClose(hFind);
    #else
        // Linux: Use glob
        glob_t glob_result;
        std::string pattern = cache_dir_ + "/*.f32";

        if (glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_result) != 0) {
            LLAMA_LOG_WARN("%s: No .f32 files found in cache directory\n", __func__);
            return 0;
        }

        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            std::string full_path = glob_result.gl_pathv[i];
            size_t last_slash = full_path.find_last_of("/\\");
            std::string filename = (last_slash != std::string::npos) ?
                                   full_path.substr(last_slash + 1) : full_path;

            if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".f32") {
                std::string tensor_name = filename.substr(0, filename.length() - 4);
                int layer_num = extract_layer_num(tensor_name);

                if (layer_num >= 0 && layer_num >= num_layers_) {
                    num_layers_ = layer_num + 1;
                }
            }
        }

        globfree(&glob_result);
    #endif

    LLAMA_LOG_INFO("%s: Found %d layers in cache\n", __func__, num_layers_);
    return num_layers_;
}

void VDPELayerManager::register_tensor(const std::string& name,
                                      llama_tensor_weight* tensor,
                                      size_t size_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);

    TensorInfo info;
    info.name = name;
    info.layer_num = extract_layer_num(name);
    info.size_bytes = size_bytes;
    info.f32_file_path = cache_dir_ + "/" + name + ".f32";
    info.state = LayerState::COLD;
    info.mapped_ptr = nullptr;
    info.last_access_time = 0;

    tensors_[name] = info;
    tensor_weights_[name] = tensor;

    // Update layer info
    if (info.layer_num >= 0) {
        LayerInfo& layer = layers_[info.layer_num];
        if (layer.layer_num == -1) {
            layer.layer_num = info.layer_num;
            layer.state = LayerState::COLD;
            layer.total_size_bytes = 0;
            layer.last_access_time = 0;
        }
        layer.tensor_names.push_back(name);
        layer.total_size_bytes += size_bytes;
    }

    stats_.total_tensors++;
    stats_.total_bytes += size_bytes;
}

// ============================================================================
// Progressive Loading
// ============================================================================

size_t VDPELayerManager::wire_initial_layers() {
    std::lock_guard<std::mutex> lock(mutex_);

    LLAMA_LOG_INFO("%s: Wiring first %d layers (progressive loading)...\n",
                   __func__, config_.max_initial_layers);

    size_t wired_count = 0;
    size_t deferred_count = 0;

    for (auto& [name, info] : tensors_) {
        // Skip tensors from layers beyond max_initial_layers
        if (info.layer_num >= config_.max_initial_layers) {
            deferred_count++;
            stats_.deferred_tensors++;
            stats_.deferred_bytes += info.size_bytes;
            continue;
        }

        // Wire tensor (non-layer tensors like embeddings always wired)
        if (wire_tensor_internal(name)) {
            wired_count++;
            if (wired_count % 20 == 0) {
                LLAMA_LOG_INFO("%s:   Wired %zu/%zu tensors...\n",
                              __func__, wired_count, tensors_.size());
            }
        }
    }

    // Update layer states
    for (int i = 0; i < config_.max_initial_layers && i < num_layers_; i++) {
        if (layers_.find(i) != layers_.end()) {
            layers_[i].state = LayerState::HOT;
            stats_.num_wired_layers++;
        }
    }

    float ram_saved_gb = (deferred_count * 65.0f) / 1024.0f;  // Rough estimate

    LLAMA_LOG_INFO("%s: Progressive loading complete:\n", __func__);
    LLAMA_LOG_INFO("%s:   - Wired:    %zu tensors (first %d layers)\n",
                  __func__, wired_count, config_.max_initial_layers);
    LLAMA_LOG_INFO("%s:   - Deferred: %zu tensors (remaining layers)\n",
                  __func__, deferred_count);
    LLAMA_LOG_INFO("%s:   - RAM saved: ~%.1f GB (deferred to disk)\n",
                  __func__, ram_saved_gb);

    stats_.wired_tensors = wired_count;
    stats_.wired_bytes = total_wired_bytes_;

    return wired_count;
}

bool VDPELayerManager::wire_layer(int layer_num) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (layers_.find(layer_num) == layers_.end()) {
        LLAMA_LOG_WARN("%s: Layer %d not found\n", __func__, layer_num);
        return false;
    }

    LayerInfo& layer = layers_[layer_num];
    if (layer.state == LayerState::HOT) {
        // Already wired
        return true;
    }

    LLAMA_LOG_INFO("%s: Dynamically wiring layer %d...\n", __func__, layer_num);
    layer.state = LayerState::WARMING;

    // Check if we need to evict
    if (config_.enable_eviction && is_memory_pressure()) {
        LLAMA_LOG_INFO("%s: Memory pressure detected, evicting LRU layer...\n", __func__);
        evict_lru_layer();
    }

    // Wire all tensors in this layer
    bool success = true;
    for (const std::string& tensor_name : layer.tensor_names) {
        if (!wire_tensor_internal(tensor_name)) {
            success = false;
            break;
        }
    }

    if (success) {
        layer.state = LayerState::HOT;
        touch_layer(layer_num);
        stats_.num_dynamic_loads++;
        LLAMA_LOG_INFO("%s: Layer %d wired successfully (%.1f MB)\n",
                      __func__, layer_num, layer.total_size_bytes / (1024.0f * 1024.0f));
    } else {
        layer.state = LayerState::COLD;
        LLAMA_LOG_ERROR("%s: Failed to wire layer %d\n", __func__, layer_num);
    }

    return success;
}

bool VDPELayerManager::wire_tensor(const std::string& tensor_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return wire_tensor_internal(tensor_name);
}

bool VDPELayerManager::wire_tensor_internal(const std::string& tensor_name) {
    // Called with mutex locked

    if (tensors_.find(tensor_name) == tensors_.end()) {
        LLAMA_LOG_WARN("%s: Tensor '%s' not registered\n", __func__, tensor_name.c_str());
        return false;
    }

    TensorInfo& info = tensors_[tensor_name];
    if (info.state == LayerState::HOT) {
        // Already wired
        return true;
    }

    // Check if .f32 file exists
    struct stat st;
    if (stat(info.f32_file_path.c_str(), &st) != 0) {
        LLAMA_LOG_WARN("%s: Tensor '%s' .f32 file not found at %s\n",
                      __func__, tensor_name.c_str(), info.f32_file_path.c_str());
        return false;
    }

    // Get expected size
    llama_tensor_weight* tensor = tensor_weights_[tensor_name];
    size_t expected_size = ggml_nelements(tensor->tensor) * sizeof(float);
    size_t file_size = st.st_size;

    if (file_size != expected_size) {
        LLAMA_LOG_ERROR("%s: Tensor '%s' size mismatch (file: %zu, expected: %zu)\n",
                       __func__, tensor_name.c_str(), file_size, expected_size);
        return false;
    }

    // Memory-map the .f32 file
    const float* mapped_ptr = workspace_->read_direct<float>(info.f32_file_path.c_str(),
                                                              ggml_nelements(tensor->tensor));
    if (mapped_ptr == nullptr) {
        LLAMA_LOG_ERROR("%s: Failed to mmap tensor '%s' from %s\n",
                       __func__, tensor_name.c_str(), info.f32_file_path.c_str());
        return false;
    }

    // Wire via external tensor API (need to call llama_model_set_external_tensor)
    // This is a placeholder - actual wiring happens in llama-model.cpp
    info.mapped_ptr = const_cast<float*>(mapped_ptr);
    info.state = LayerState::HOT;
    touch_tensor(tensor_name);

    total_wired_bytes_ += info.size_bytes;

    return true;
}

// ============================================================================
// Layer Eviction
// ============================================================================

bool VDPELayerManager::is_memory_pressure() const {
    return total_wired_bytes_ > get_ram_budget_bytes();
}

bool VDPELayerManager::evict_lru_layer() {
    std::lock_guard<std::mutex> lock(mutex_);

    int lru_layer = find_lru_layer();
    if (lru_layer < 0) {
        LLAMA_LOG_WARN("%s: No evictable layers found\n", __func__);
        return false;
    }

    return evict_layer(lru_layer);
}

bool VDPELayerManager::evict_layer(int layer_num) {
    // Called with mutex locked

    if (layers_.find(layer_num) == layers_.end()) {
        LLAMA_LOG_WARN("%s: Layer %d not found\n", __func__, layer_num);
        return false;
    }

    LayerInfo& layer = layers_[layer_num];
    if (layer.state != LayerState::HOT) {
        // Not wired, nothing to evict
        return true;
    }

    LLAMA_LOG_INFO("%s: Evicting layer %d (%.1f MB)...\n",
                  __func__, layer_num, layer.total_size_bytes / (1024.0f * 1024.0f));

    layer.state = LayerState::EVICTING;

    // Unmap all tensors in this layer
    for (const std::string& tensor_name : layer.tensor_names) {
        unmap_tensor(tensor_name);
    }

    layer.state = LayerState::COLD;
    stats_.num_evictions++;

    LLAMA_LOG_INFO("%s: Layer %d evicted successfully\n", __func__, layer_num);
    return true;
}

void VDPELayerManager::unmap_tensor(const std::string& tensor_name) {
    // Called with mutex locked

    if (tensors_.find(tensor_name) == tensors_.end()) {
        return;
    }

    TensorInfo& info = tensors_[tensor_name];
    if (info.mapped_ptr != nullptr) {
        // Unmapping is handled by workspace destructor
        info.mapped_ptr = nullptr;
        info.state = LayerState::COLD;

        if (total_wired_bytes_ >= info.size_bytes) {
            total_wired_bytes_ -= info.size_bytes;
        }
    }
}

// ============================================================================
// Inference Callbacks
// ============================================================================

void VDPELayerManager::on_before_layer_compute(int layer_num) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (layers_.find(layer_num) == layers_.end()) {
        return;
    }

    LayerInfo& layer = layers_[layer_num];
    if (layer.state == LayerState::COLD) {
        // Layer not loaded, trigger dynamic loading
        LLAMA_LOG_INFO("%s: Layer %d needed, triggering dynamic load...\n",
                      __func__, layer_num);
        wire_layer(layer_num);
    }

    // Prefetch next layer if enabled
    if (config_.enable_prefetch) {
        int next_layer = layer_num + config_.prefetch_lookahead;
        if (next_layer < num_layers_ && layers_.find(next_layer) != layers_.end()) {
            if (layers_[next_layer].state == LayerState::COLD) {
                LLAMA_LOG_INFO("%s: Prefetching layer %d...\n", __func__, next_layer);
                wire_layer(next_layer);
            }
        }
    }
}

void VDPELayerManager::on_after_layer_compute(int layer_num) {
    std::lock_guard<std::mutex> lock(mutex_);
    touch_layer(layer_num);
}

// ============================================================================
// State Queries
// ============================================================================

LayerState VDPELayerManager::get_layer_state(int layer_num) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = layers_.find(layer_num);
    if (it == layers_.end()) {
        return LayerState::COLD;
    }
    return it->second.state;
}

LayerState VDPELayerManager::get_tensor_state(const std::string& tensor_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tensors_.find(tensor_name);
    if (it == tensors_.end()) {
        return LayerState::COLD;
    }
    return it->second.state;
}

int VDPELayerManager::get_num_wired_layers() const {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = 0;
    for (const auto& [num, layer] : layers_) {
        if (layer.state == LayerState::HOT) {
            count++;
        }
    }
    return count;
}

int VDPELayerManager::get_num_deferred_layers() const {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = 0;
    for (const auto& [num, layer] : layers_) {
        if (layer.state == LayerState::COLD) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Statistics
// ============================================================================

VDPELayerManager::Stats VDPELayerManager::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats = stats_;
    stats.num_layers = num_layers_;
    stats.num_wired_layers = get_num_wired_layers();
    return stats;
}

void VDPELayerManager::print_stats() const {
    Stats stats = get_stats();

    LLAMA_LOG_INFO("=== VDPELayerManager Statistics ===\n");
    LLAMA_LOG_INFO("Tensors:        %zu total, %zu wired, %zu deferred\n",
                  stats.total_tensors, stats.wired_tensors, stats.deferred_tensors);
    LLAMA_LOG_INFO("Bytes:          %.2f GB total, %.2f GB wired, %.2f GB deferred\n",
                  stats.total_bytes / (1024.0 * 1024.0 * 1024.0),
                  stats.wired_bytes / (1024.0 * 1024.0 * 1024.0),
                  stats.deferred_bytes / (1024.0 * 1024.0 * 1024.0));
    LLAMA_LOG_INFO("Layers:         %d total, %d wired\n",
                  stats.num_layers, stats.num_wired_layers);
    LLAMA_LOG_INFO("Operations:     %d evictions, %d dynamic loads\n",
                  stats.num_evictions, stats.num_dynamic_loads);
    LLAMA_LOG_INFO("Memory:         %.2f GB / %.2f GB RAM used\n",
                  get_total_wired_bytes() / (1024.0 * 1024.0 * 1024.0),
                  get_ram_budget_bytes() / (1024.0 * 1024.0 * 1024.0));
}

// ============================================================================
// Helper Methods
// ============================================================================

int VDPELayerManager::extract_layer_num(const std::string& tensor_name) const {
    // Extract layer number from tensor name (e.g., "blk.0.attn_q.weight" -> 0)
    if (tensor_name.find("blk.") != 0) {
        return -1;  // Not a layer tensor
    }

    size_t dot_pos = tensor_name.find('.', 4);
    if (dot_pos == std::string::npos) {
        return -1;
    }

    try {
        return std::stoi(tensor_name.substr(4, dot_pos - 4));
    } catch (...) {
        return -1;
    }
}

void VDPELayerManager::touch_layer(int layer_num) {
    // Called with mutex locked

    if (layers_.find(layer_num) == layers_.end()) {
        return;
    }

    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    layers_[layer_num].last_access_time = now;
}

void VDPELayerManager::touch_tensor(const std::string& tensor_name) {
    // Called with mutex locked

    if (tensors_.find(tensor_name) == tensors_.end()) {
        return;
    }

    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    tensors_[tensor_name].last_access_time = now;

    // Also touch the layer
    int layer_num = tensors_[tensor_name].layer_num;
    if (layer_num >= 0) {
        touch_layer(layer_num);
    }
}

int VDPELayerManager::find_lru_layer() const {
    // Called with mutex locked

    int lru_layer = -1;
    uint64_t oldest_time = UINT64_MAX;

    for (const auto& [num, layer] : layers_) {
        if (layer.state == LayerState::HOT && layer.last_access_time < oldest_time) {
            oldest_time = layer.last_access_time;
            lru_layer = num;
        }
    }

    return lru_layer;
}

} // namespace vdpe
