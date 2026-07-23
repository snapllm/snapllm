/**
 * @file dequant_cache.h
 * @brief Dequantized Weight Cache - Zero Dequantization Overhead
 * 
 * Indexes model metadata associated with a vPID workspace.
 *
 * Direct GGUF parsing/dequantization through this class is intentionally
 * unavailable. Production model loading is owned by VPIDBridge/llama.cpp.
 */

#pragma once

#include "vpid_workspace.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace snapllm {

/**
 * @brief Tensor metadata
 */
struct TensorInfo {
    std::string name;
    std::vector<int64_t> shape;
    size_t num_elements;
    size_t byte_size;
    
    // vPID storage location
    size_t vpid_offset;
    VPIDAllocation vpid_alloc;
    
    // Access tracking
    uint64_t access_count;
    
    TensorInfo() : num_elements(0), byte_size(0), vpid_offset(0), access_count(0) {}
};

/**
 * @brief Model metadata loaded from GGUF
 */
struct ModelInfo {
    std::string name;
    std::string architecture;
    int64_t vocab_size;
    int64_t context_length;
    int64_t embedding_length;
    int64_t num_layers;
    int64_t num_heads;
    int64_t num_kv_heads;
    
    // Tensor catalog
    std::vector<TensorInfo> tensors;
    std::unordered_map<std::string, size_t> tensor_index;  // name -> index
    
    ModelInfo() 
        : vocab_size(0), context_length(0), embedding_length(0),
          num_layers(0), num_heads(0), num_kv_heads(0) {}
};

/**
 * @brief Dequantized Cache Manager
 * 
 * Dequantize at startup and store reusable tensors in the vPID workspace.
 * 
 * Strategy:
 * 1. Load GGUF model (quantized)
 * 2. Dequantize tensors to F32
 * 3. Store F32 in vPID workspace (persistent across sessions)
 * 4. Reuse the cached F32 representation during inference
 * 
 * Runtime performance depends on the model, hardware, and cache state.
 * 
 * Example:
 * @code
 * DequantCache cache(vpid_workspace);
 * 
 * // Production code registers models after VPIDBridge/llama.cpp loads them.
 * 
 * Direct tensor pointers are intentionally internal to VPIDBridge so their
 * lifetime remains protected by the bridge's model lifecycle lock.
 * @endcode
 */
class DequantCache {
public:
    /**
     * @brief Constructor
     * @param vpid vPID workspace for storage
     */
    explicit DequantCache(std::shared_ptr<VPIDWorkspace> vpid);

private:
    friend class VPIDBridge;

    /**
     * @brief Unload model from memory (keeps in vPID)
     * @param model_name Model to unload
     */
    void unload_model(const std::string& model_name);
    
    /**
     * @brief Get tensor data (F32, zero-copy)
     * @param model_name Model name
     * @param tensor_name Tensor name (e.g., "blk.0.attn_q.weight")
     * @return Pointer to F32 data, or nullptr if not found
     */
    const float* get_tensor(const std::string& model_name,
                           const std::string& tensor_name);
    
    /**
     * @brief Get tensor info
     * @param model_name Model name
     * @param tensor_name Tensor name
     * @return Tensor info, or nullptr if not found
     */
    const TensorInfo* get_tensor_info(const std::string& model_name,
                                      const std::string& tensor_name);
    
    /**
     * @brief Get model info
     * @param model_name Model name
     * @return Model info, or nullptr if not found
     */
    const ModelInfo* get_model_info(const std::string& model_name);
    
    /**
     * @brief Check if model is loaded
     */
    bool is_model_loaded(const std::string& model_name) const;
    
    /**
     * @brief Get list of loaded models
     */
    std::vector<std::string> get_loaded_models() const;
    
    /**
     * @brief Get cache statistics
     */
    struct CacheStats {
        size_t num_models;
        size_t num_tensors;
        size_t total_bytes;
        uint64_t total_accesses;
        double avg_access_time_ms;
    };
    
    CacheStats get_stats() const;
    
    /**
     * @brief Save cache metadata (for persistence)
     */
    bool save_metadata(const std::string& path);
    
    /**
     * @brief Load cache metadata (for fast restart)
     */
    bool load_metadata(const std::string& path);

    /**
     * @brief Register a model after manual tensor loading
     * @param model_name Model name
     * @param gguf_path Original GGUF path
     * @param num_tensors Number of tensors loaded
     * @param total_size Total size in bytes
     * @return true if successful
     */
    bool register_model(const std::string& model_name,
                       const std::string& gguf_path,
                       size_t num_tensors,
                       size_t total_size);

    /**
     * @brief Register a model with full metadata
     * @param model_info Complete model information including tensors
     * @return true if successful
     */
    bool register_model_with_metadata(const ModelInfo& model_info);

    /**
     * @brief Get VPIDWorkspace for direct access
     */
    std::shared_ptr<VPIDWorkspace> get_vpid() { return vpid_; }

    std::shared_ptr<VPIDWorkspace> vpid_;
    std::unordered_map<std::string, ModelInfo> models_;
    mutable std::mutex cache_mutex_;
    
    // Dequantization functions
    std::vector<float> dequantize_tensor(const void* data,
                                        size_t num_elements,
                                        int ggml_type);
    
    std::vector<float> dequantize_q4_0(const void* data, size_t num_elements);
    std::vector<float> dequantize_q5_0(const void* data, size_t num_elements);
    std::vector<float> dequantize_q5_k(const void* data, size_t num_elements);
    std::vector<float> dequantize_q8_0(const void* data, size_t num_elements);
    std::vector<float> dequantize_f16(const void* data, size_t num_elements);
    
};

} // namespace snapllm
