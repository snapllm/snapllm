/**
 * @file dequant_cache.cpp
 * @brief Dequantized Weight Cache Implementation
 */

#include "snapllm/dequant_cache.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>

namespace snapllm {

DequantCache::DequantCache(std::shared_ptr<VPIDWorkspace> vpid)
    : vpid_(vpid)
{
}

bool DequantCache::load_model(const std::string& model_name,
                              const std::string& gguf_path,
                              bool force_reload) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Check if already loaded
    if (!force_reload && models_.find(model_name) != models_.end()) {
        std::cout << "Model '" << model_name << "' already loaded" << std::endl;
        return true;
    }
    
    std::cout << "Loading model: " << model_name << std::endl;
    std::cout << "  From: " << gguf_path << std::endl;
    
    // Parse GGUF file
    ModelInfo model_info;
    model_info.name = model_name;
    
    if (!parse_gguf(gguf_path, model_info)) {
        std::cerr << "Failed to parse GGUF file" << std::endl;
        return false;
    }
    
    std::cout << "  Parsed GGUF: " << model_info.tensors.size() << " tensors" << std::endl;
    
    // Dequantize and store each tensor
    std::cout << "  Dequantizing tensors..." << std::endl;
    size_t total_size = 0;
    
    for (auto& tensor : model_info.tensors) {
        // TODO: In full implementation, load actual tensor data from GGUF
        // For now, allocate space in vPID
        
        tensor.byte_size = tensor.num_elements * sizeof(float);
        total_size += tensor.byte_size;
        
        // Allocate in vPID
        tensor.vpid_alloc = vpid_->allocate(tensor.byte_size, tensor.name);
        tensor.vpid_offset = tensor.vpid_alloc.offset;
        
        // TODO: Actual dequantization would happen here
        // std::vector<float> dequantized = dequantize_tensor(...);
        // vpid_->write_direct(tensor.vpid_offset, dequantized.data(), tensor.byte_size);
    }
    
    std::cout << "  Total dequantized size: " << (total_size / (1024.0 * 1024 * 1024)) << " GB" << std::endl;
    
    // Store model info
    models_[model_name] = std::move(model_info);
    
    std::cout << "  Model loaded successfully!" << std::endl;
    return true;
}

void DequantCache::unload_model(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = models_.find(model_name);
    if (it == models_.end()) {
        std::cerr << "Model not found: " << model_name << std::endl;
        return;
    }
    
    // Free vPID allocations
    for (const auto& tensor : it->second.tensors) {
        vpid_->free(tensor.vpid_alloc);
    }
    
    models_.erase(it);
    std::cout << "Model unloaded: " << model_name << std::endl;
}

const float* DequantCache::get_tensor(const std::string& model_name,
                                     const std::string& tensor_name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    // DEBUG: First tensor lookup
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        std::cout << "\n[DequantCache::get_tensor DEBUG]" << std::endl;
        std::cout << "  Looking for model: '" << model_name << "'" << std::endl;
        std::cout << "  Total models in cache: " << models_.size() << std::endl;
        for (const auto& pair : models_) {
            std::cout << "    Model key: '" << pair.first << "'" << std::endl;
        }
    }

    auto model_it = models_.find(model_name);
    if (model_it == models_.end()) {
        if (first_call) std::cout << "  Model NOT FOUND!" << std::endl;
        return nullptr;
    }

    auto& model = model_it->second;

    // DEBUG: Print tensor_index info on first call
    if (first_call) {
        std::cout << "  Model found! tensor_index size: " << model.tensor_index.size() << std::endl;
        std::cout << "  First 5 tensor names in index:" << std::endl;
        int count = 0;
        for (const auto& pair : model.tensor_index) {
            if (count++ >= 5) break;
            std::cout << "    '" << pair.first << "'" << std::endl;
        }
        std::cout << "  Looking for tensor: '" << tensor_name << "'" << std::endl;
    }

    auto tensor_it = model.tensor_index.find(tensor_name);
    if (tensor_it == model.tensor_index.end()) {
        return nullptr;
    }

    auto& tensor = model.tensors[tensor_it->second];
    tensor.access_count++;

    // Return pointer to vPID data (zero-copy!)
    return vpid_->read_direct<float>(tensor.vpid_offset, tensor.num_elements);
}

const TensorInfo* DequantCache::get_tensor_info(const std::string& model_name,
                                                const std::string& tensor_name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto model_it = models_.find(model_name);
    if (model_it == models_.end()) {
        return nullptr;
    }
    
    auto& model = model_it->second;
    auto tensor_it = model.tensor_index.find(tensor_name);
    if (tensor_it == model.tensor_index.end()) {
        return nullptr;
    }
    
    return &model.tensors[tensor_it->second];
}

const ModelInfo* DequantCache::get_model_info(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = models_.find(model_name);
    return (it != models_.end()) ? &it->second : nullptr;
}

bool DequantCache::is_model_loaded(const std::string& model_name) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return models_.find(model_name) != models_.end();
}

std::vector<std::string> DequantCache::get_loaded_models() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::vector<std::string> names;
    names.reserve(models_.size());
    
    for (const auto& pair : models_) {
        names.push_back(pair.first);
    }
    
    return names;
}

DequantCache::CacheStats DequantCache::get_stats() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    CacheStats stats;
    stats.num_models = models_.size();
    stats.num_tensors = 0;
    stats.total_bytes = 0;
    stats.total_accesses = 0;
    
    for (const auto& pair : models_) {
        stats.num_tensors += pair.second.tensors.size();
        for (const auto& tensor : pair.second.tensors) {
            stats.total_bytes += tensor.byte_size;
            stats.total_accesses += tensor.access_count;
        }
    }
    
    return stats;
}

bool DequantCache::parse_gguf(const std::string& path, ModelInfo& model_info) {
    // Simplified GGUF parser
    // In production, integrate with llama.cpp's gguf parser
    
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return false;
    }
    
    // Read GGUF magic
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    
    if (magic != 0x46554747) {  // "GGUF" in little-endian
        std::cerr << "Invalid GGUF magic number" << std::endl;
        return false;
    }
    
    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    std::cout << "    GGUF version: " << version << std::endl;
    
    // TODO: Full GGUF parsing implementation
    // For now, create dummy model info for demonstration
    model_info.architecture = "llama";
    model_info.vocab_size = 32000;
    model_info.context_length = 4096;
    model_info.embedding_length = 4096;
    model_info.num_layers = 32;
    model_info.num_heads = 32;
    model_info.num_kv_heads = 32;
    
    // Create dummy tensors
    // In production, these would be parsed from GGUF
    for (int i = 0; i < model_info.num_layers; i++) {
        TensorInfo tensor;
        tensor.name = "blk." + std::to_string(i) + ".attn_q.weight";
        tensor.shape = {4096, 4096};
        tensor.num_elements = 4096 * 4096;
        model_info.tensors.push_back(tensor);
        model_info.tensor_index[tensor.name] = model_info.tensors.size() - 1;
    }
    
    return true;
}

// Dequantization implementations
// These are simplified - production would use llama.cpp's optimized kernels

std::vector<float> DequantCache::dequantize_q4_0(const void* data, size_t num_elements) {
    std::vector<float> result(num_elements);
    // TODO: Implement Q4_0 dequantization
    return result;
}

std::vector<float> DequantCache::dequantize_q5_0(const void* data, size_t num_elements) {
    std::vector<float> result(num_elements);
    // TODO: Implement Q5_0 dequantization
    return result;
}

std::vector<float> DequantCache::dequantize_q5_k(const void* data, size_t num_elements) {
    std::vector<float> result(num_elements);
    // TODO: Implement Q5_K dequantization
    return result;
}

std::vector<float> DequantCache::dequantize_q8_0(const void* data, size_t num_elements) {
    std::vector<float> result(num_elements);
    // TODO: Implement Q8_0 dequantization
    return result;
}

std::vector<float> DequantCache::dequantize_f16(const void* data, size_t num_elements) {
    std::vector<float> result(num_elements);
    // TODO: Implement F16 dequantization
    return result;
}

bool DequantCache::register_model(const std::string& model_name,
                                  const std::string& gguf_path,
                                  size_t num_tensors,
                                  size_t total_size) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    // Create model info entry
    ModelInfo model_info;
    model_info.name = model_name;

    // Store in models map
    models_[model_name] = model_info;

    std::cout << "  Model '" << model_name << "' registered successfully" << std::endl;
    std::cout << "    Tensors: " << num_tensors << std::endl;
    std::cout << "    Total size: " << (total_size / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;

    return true;
}

bool DequantCache::register_model_with_metadata(const ModelInfo& model_info) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    // Store model with full metadata
    models_[model_info.name] = model_info;

    // Build tensor index
    auto& model = models_[model_info.name];
    for (size_t i = 0; i < model.tensors.size(); i++) {
        model.tensor_index[model.tensors[i].name] = i;
    }

    std::cout << "  Model '" << model_info.name << "' registered with full metadata" << std::endl;
    std::cout << "    Tensors: " << model.tensors.size() << std::endl;

    size_t total_size = 0;
    for (const auto& tensor : model.tensors) {
        total_size += tensor.byte_size;
    }
    std::cout << "    Total size: " << (total_size / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;

    return true;
}

bool DequantCache::save_metadata(const std::string& path) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open metadata file for writing: " << path << std::endl;
        return false;
    }

    // Write header
    file << "# SnapLLM DequantCache Metadata\n";
    file << "# Version: 1.0\n";
    file << "num_models=" << models_.size() << "\n";
    file << "\n";

    // Write each model
    for (const auto& model_pair : models_) {
        const ModelInfo& model = model_pair.second;

        file << "[model]\n";
        file << "name=" << model.name << "\n";
        file << "architecture=" << model.architecture << "\n";
        file << "vocab_size=" << model.vocab_size << "\n";
        file << "context_length=" << model.context_length << "\n";
        file << "embedding_length=" << model.embedding_length << "\n";
        file << "num_layers=" << model.num_layers << "\n";
        file << "num_heads=" << model.num_heads << "\n";
        file << "num_kv_heads=" << model.num_kv_heads << "\n";
        file << "num_tensors=" << model.tensors.size() << "\n";

        // Write tensor information
        for (const auto& tensor : model.tensors) {
            file << "[tensor]\n";
            file << "name=" << tensor.name << "\n";
            file << "num_elements=" << tensor.num_elements << "\n";
            file << "byte_size=" << tensor.byte_size << "\n";
            file << "vpid_offset=" << tensor.vpid_offset << "\n";
            file << "access_count=" << tensor.access_count << "\n";

            // Write shape
            file << "shape=";
            for (size_t i = 0; i < tensor.shape.size(); i++) {
                if (i > 0) file << ",";
                file << tensor.shape[i];
            }
            file << "\n";
        }

        file << "\n";
    }

    file.close();
    std::cout << "Metadata saved to: " << path << std::endl;
    std::cout << "  Models: " << models_.size() << std::endl;

    return true;
}

bool DequantCache::load_metadata(const std::string& path) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open metadata file for reading: " << path << std::endl;
        return false;
    }

    models_.clear();

    std::string line;
    ModelInfo* current_model = nullptr;
    TensorInfo* current_tensor = nullptr;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        // Parse model section
        if (line == "[model]") {
            if (current_model != nullptr) {
                // Save previous model
                models_[current_model->name] = *current_model;
            }
            current_model = new ModelInfo();
            current_tensor = nullptr;
            continue;
        }

        // Parse tensor section
        if (line == "[tensor]") {
            if (current_model != nullptr) {
                current_model->tensors.emplace_back();
                current_tensor = &current_model->tensors.back();
            }
            continue;
        }

        // Parse key=value pairs
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Model fields
        if (current_model != nullptr && current_tensor == nullptr) {
            if (key == "name") current_model->name = value;
            else if (key == "architecture") current_model->architecture = value;
            else if (key == "vocab_size") current_model->vocab_size = std::stoll(value);
            else if (key == "context_length") current_model->context_length = std::stoll(value);
            else if (key == "embedding_length") current_model->embedding_length = std::stoll(value);
            else if (key == "num_layers") current_model->num_layers = std::stoll(value);
            else if (key == "num_heads") current_model->num_heads = std::stoll(value);
            else if (key == "num_kv_heads") current_model->num_kv_heads = std::stoll(value);
        }
        // Tensor fields
        else if (current_tensor != nullptr) {
            if (key == "name") current_tensor->name = value;
            else if (key == "num_elements") current_tensor->num_elements = std::stoull(value);
            else if (key == "byte_size") current_tensor->byte_size = std::stoull(value);
            else if (key == "vpid_offset") current_tensor->vpid_offset = std::stoull(value);
            else if (key == "access_count") current_tensor->access_count = std::stoull(value);
            else if (key == "shape") {
                // Parse comma-separated shape
                std::stringstream ss(value);
                std::string dim;
                while (std::getline(ss, dim, ',')) {
                    current_tensor->shape.push_back(std::stoll(dim));
                }
            }
        }
    }

    // Save last model
    if (current_model != nullptr) {
        models_[current_model->name] = *current_model;
        delete current_model;

        // Build tensor index
        for (auto& model_pair : models_) {
            auto& model = model_pair.second;
            for (size_t i = 0; i < model.tensors.size(); i++) {
                model.tensor_index[model.tensors[i].name] = i;
            }
        }
    }

    file.close();
    std::cout << "Metadata loaded from: " << path << std::endl;
    std::cout << "  Models: " << models_.size() << std::endl;

    return true;
}

} // namespace snapllm
