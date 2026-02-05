/**
 * @file workspace_metadata.h
 * @brief Workspace metadata manager for persistent model caching
 *
 * Organizes models by name and quantization type in a structured workspace.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>

namespace snapllm {

/**
 * @brief Tensor location in vPID workspace
 */
struct TensorLocation {
    std::string name;
    size_t vpid_offset;      // Offset in vPID workspace
    size_t size_bytes;
    size_t element_count;
    std::string original_type;  // q8_0, q5_k_m, etc
    std::string dequant_type;   // Always f32
};

/**
 * @brief Model metadata
 */
struct ModelMetadata {
    std::string name;
    std::string gguf_path;
    std::string gguf_hash;      // SHA256 hash to detect changes
    std::string quant_type;     // Q8_0, Q5_K_M, etc
    std::string architecture;   // llama, mpt, etc
    size_t tensor_count;
    size_t total_size_bytes;
    size_t vocab_size;
    size_t context_length;
    size_t embedding_length;
    size_t layer_count;
    std::string loaded_timestamp;
    std::vector<TensorLocation> tensors;
};

/**
 * @brief Workspace index entry
 */
struct WorkspaceIndexEntry {
    std::string name;
    std::string quant_type;
    std::string gguf_path;
    size_t tensor_count;
    size_t total_size_bytes;
    std::string loaded_timestamp;
    std::string metadata_path;  // Relative path to model metadata
};

/**
 * @brief Workspace metadata manager
 *
 * Manages persistent storage of model metadata in organized structure.
 * Allows checking if a model is already cached before dequantizing.
 */
class WorkspaceMetadata {
public:
    explicit WorkspaceMetadata(const std::string& workspace_path);

    // Initialize workspace structure
    bool initialize();

    // Check if model exists in cache
    bool model_exists(const std::string& model_name, const std::string& quant_type) const;

    // Get model metadata
    ModelMetadata get_model_metadata(const std::string& model_name, const std::string& quant_type) const;

    // Save model metadata
    bool save_model_metadata(const ModelMetadata& metadata);

    // Remove model from workspace
    bool remove_model(const std::string& model_name, const std::string& quant_type);

    // List all cached models
    std::vector<WorkspaceIndexEntry> list_models() const;

    // Get workspace statistics
    size_t get_total_cached_size() const;
    size_t get_model_count() const;

    // Extract quant type from GGUF filename
    static std::string extract_quant_type(const std::string& gguf_path);

    // Extract model name from GGUF path
    static std::string extract_model_name(const std::string& gguf_path);

private:
    std::string workspace_path_;
    std::string metadata_dir_;
    std::string index_path_;

    // In-memory index
    std::vector<WorkspaceIndexEntry> index_;

    // Helper methods
    bool load_index();
    bool save_index();
    bool create_directory_structure();
    std::string get_model_dir(const std::string& model_name, const std::string& quant_type) const;
    bool load_model_json(const std::string& path, ModelMetadata& metadata) const;
    bool save_model_json(const std::string& path, const ModelMetadata& metadata);
    bool load_tensors_json(const std::string& path, std::vector<TensorLocation>& tensors) const;
    bool save_tensors_json(const std::string& path, const std::vector<TensorLocation>& tensors);
};

} // namespace snapllm
