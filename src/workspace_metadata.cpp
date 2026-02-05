/**
 * @file workspace_metadata.cpp
 * @brief Workspace metadata manager implementation
 */

#include "snapllm/workspace_metadata.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cctype>

// Simple JSON serialization (can be replaced with proper JSON library if needed)
namespace {

std::string escape_json_string(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string read_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.length();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";

    return json.substr(pos, end - pos);
}

size_t read_json_number(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;

    pos += search.length();
    size_t end = json.find_first_of(",}", pos);
    if (end == std::string::npos) return 0;

    std::string num_str = json.substr(pos, end - pos);
    try {
        return std::stoull(num_str);
    } catch (...) {
        return 0;
    }
}

} // anonymous namespace

namespace snapllm {

WorkspaceMetadata::WorkspaceMetadata(const std::string& workspace_path)
    : workspace_path_(workspace_path)
    , metadata_dir_(workspace_path)
    , index_path_(workspace_path + "/index.json")
{
}

bool WorkspaceMetadata::initialize() {
    // Create workspace directory if it doesn't exist
    if (!create_directory_structure()) {
        std::cerr << "Failed to create workspace directory structure" << std::endl;
        return false;
    }

    // Load existing index or create new one
    if (!load_index()) {
        std::cout << "Creating new workspace index" << std::endl;
        index_.clear();
        if (!save_index()) {
            std::cerr << "Failed to create initial index" << std::endl;
            return false;
        }
    }

    return true;
}

bool WorkspaceMetadata::model_exists(const std::string& model_name, const std::string& quant_type) const {
    for (const auto& entry : index_) {
        if (entry.name == model_name && entry.quant_type == quant_type) {
            return true;
        }
    }
    return false;
}

ModelMetadata WorkspaceMetadata::get_model_metadata(const std::string& model_name, const std::string& quant_type) const {
    ModelMetadata metadata;

    // Find entry in index
    for (const auto& entry : index_) {
        if (entry.name == model_name && entry.quant_type == quant_type) {
            // Load metadata from JSON files
            std::string model_dir = get_model_dir(model_name, quant_type);
            std::string metadata_path = model_dir + "/metadata.json";
            std::string tensors_path = model_dir + "/tensors.json";

            if (!load_model_json(metadata_path, metadata)) {
                std::cerr << "Failed to load model metadata from " << metadata_path << std::endl;
                return metadata;
            }

            if (!load_tensors_json(tensors_path, metadata.tensors)) {
                std::cerr << "Failed to load tensor metadata from " << tensors_path << std::endl;
                return metadata;
            }

            return metadata;
        }
    }

    return metadata;
}

bool WorkspaceMetadata::save_model_metadata(const ModelMetadata& metadata) {
    // Create model directory
    std::string model_dir = get_model_dir(metadata.name, metadata.quant_type);
    std::filesystem::create_directories(model_dir);

    // Save metadata JSON
    std::string metadata_path = model_dir + "/metadata.json";
    if (!save_model_json(metadata_path, metadata)) {
        std::cerr << "Failed to save model metadata" << std::endl;
        return false;
    }

    // Save tensors JSON
    std::string tensors_path = model_dir + "/tensors.json";
    if (!save_tensors_json(tensors_path, metadata.tensors)) {
        std::cerr << "Failed to save tensor metadata" << std::endl;
        return false;
    }

    // Update index
    bool found = false;
    for (auto& entry : index_) {
        if (entry.name == metadata.name && entry.quant_type == metadata.quant_type) {
            entry.tensor_count = metadata.tensor_count;
            entry.total_size_bytes = metadata.total_size_bytes;
            entry.loaded_timestamp = metadata.loaded_timestamp;
            entry.gguf_path = metadata.gguf_path;
            found = true;
            break;
        }
    }

    if (!found) {
        WorkspaceIndexEntry entry;
        entry.name = metadata.name;
        entry.quant_type = metadata.quant_type;
        entry.gguf_path = metadata.gguf_path;
        entry.tensor_count = metadata.tensor_count;
        entry.total_size_bytes = metadata.total_size_bytes;
        entry.loaded_timestamp = metadata.loaded_timestamp;
        entry.metadata_path = metadata.name + "/" + metadata.quant_type + "/metadata.json";
        index_.push_back(entry);
    }

    return save_index();
}

bool WorkspaceMetadata::remove_model(const std::string& model_name, const std::string& quant_type) {
    // Remove from index
    auto it = std::remove_if(index_.begin(), index_.end(),
        [&](const WorkspaceIndexEntry& entry) {
            return entry.name == model_name && entry.quant_type == quant_type;
        });

    if (it == index_.end()) {
        return false;  // Not found
    }

    index_.erase(it, index_.end());

    // Delete directory
    std::string model_dir = get_model_dir(model_name, quant_type);
    try {
        std::filesystem::remove_all(model_dir);
    } catch (const std::exception& e) {
        std::cerr << "Failed to remove model directory: " << e.what() << std::endl;
        return false;
    }

    return save_index();
}

std::vector<WorkspaceIndexEntry> WorkspaceMetadata::list_models() const {
    return index_;
}

size_t WorkspaceMetadata::get_total_cached_size() const {
    size_t total = 0;
    for (const auto& entry : index_) {
        total += entry.total_size_bytes;
    }
    return total;
}

size_t WorkspaceMetadata::get_model_count() const {
    return index_.size();
}

std::string WorkspaceMetadata::extract_quant_type(const std::string& gguf_path) {
    // Extract quantization type from filename
    // Examples: "medicine-llm.Q8_0.gguf" -> "Q8_0"
    //           "llama-7b-q5_k_m.gguf" -> "Q5_K_M"

    std::filesystem::path path(gguf_path);
    std::string filename = path.stem().string();

    // Look for quantization patterns
    std::vector<std::string> patterns = {
        // Standard quantizations
        "Q8_0", "Q8_1",
        "Q4_0", "Q4_1",
        "Q5_0", "Q5_1",
        "Q2_K", "Q3_K", "Q4_K", "Q5_K", "Q6_K", "Q8_K",
        "Q3_K_S", "Q3_K_M", "Q3_K_L",
        "Q4_K_S", "Q4_K_M",
        "Q5_K_S", "Q5_K_M",
        "Q6_K",
        // IQ (importance matrix) quantizations
        "IQ1_S", "IQ1_M", "IQ2_XXS", "IQ2_XS", "IQ2_S", "IQ2_M",
        "IQ3_XXS", "IQ3_XS", "IQ3_S", "IQ3_M",
        "IQ4_XS", "IQ4_NL",
        // MXFP (Microscaling Floating Point) - used for MoE models
        "MXFP4", "MXFP6", "MXFP8",
        // BF16/F16
        "BF16", "F16"
    };

    // Convert filename to uppercase for case-insensitive matching
    std::string upper_filename = filename;
    std::transform(upper_filename.begin(), upper_filename.end(), upper_filename.begin(), ::toupper);

    for (const auto& pattern : patterns) {
        // Check if pattern exists in uppercase filename
        std::string upper_pattern = pattern;
        std::transform(upper_pattern.begin(), upper_pattern.end(), upper_pattern.begin(), ::toupper);

        size_t pos = upper_filename.find(upper_pattern);
        if (pos != std::string::npos) {
            // Return in standard uppercase format
            return pattern;
        }
    }

    // Default to F32 if no quantization found
    return "F32";
}

std::string WorkspaceMetadata::extract_model_name(const std::string& gguf_path) {
    // Extract model name from path
    // Remove quantization suffix and extension

    std::filesystem::path path(gguf_path);
    std::string filename = path.stem().string();

    // Remove quantization suffix
    std::string quant_type = extract_quant_type(gguf_path);
    size_t quant_pos = filename.find(quant_type);

    if (quant_pos != std::string::npos) {
        // Remove quant type and any separator before it
        filename = filename.substr(0, quant_pos);

        // Remove trailing separators (., -, _)
        while (!filename.empty() &&
               (filename.back() == '.' || filename.back() == '-' || filename.back() == '_')) {
            filename.pop_back();
        }
    }

    return filename.empty() ? "unknown" : filename;
}

// Private helper methods

bool WorkspaceMetadata::load_index() {
    std::cout << "  [DEBUG] Loading index from: " << index_path_ << std::endl;
    std::ifstream file(index_path_);
    if (!file.is_open()) {
        std::cout << "  [DEBUG] Index file does not exist" << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    std::cout << "  [DEBUG] Index JSON length: " << json.length() << " bytes" << std::endl;

    index_.clear();

    // Simple JSON parsing (parse array of entries)
    size_t pos = json.find("\"models\":");
    if (pos == std::string::npos) {
        std::cout << "  [DEBUG] '\"models\":' not found in JSON" << std::endl;
        return true;  // Empty index
    }
    std::cout << "  [DEBUG] Found '\"models\":' at position " << pos << std::endl;

    // Find the opening bracket (skip optional whitespace)
    pos = json.find('[', pos);
    if (pos == std::string::npos) {
        std::cout << "  [DEBUG] '[' not found after '\"models\":'" << std::endl;
        return true;  // Empty index
    }
    std::cout << "  [DEBUG] Found '[' at position " << pos << std::endl;

    pos += 1;  // Skip [
    size_t end = json.find(']', pos);
    if (end == std::string::npos) {
        return false;
    }

    std::string models_json = json.substr(pos, end - pos);

    // Parse each model entry
    size_t entry_start = 0;
    while ((entry_start = models_json.find('{', entry_start)) != std::string::npos) {
        size_t entry_end = models_json.find('}', entry_start);
        if (entry_end == std::string::npos) break;

        std::string entry_json = models_json.substr(entry_start, entry_end - entry_start + 1);

        WorkspaceIndexEntry entry;
        entry.name = read_json_string(entry_json, "name");
        entry.quant_type = read_json_string(entry_json, "quant_type");
        entry.gguf_path = read_json_string(entry_json, "gguf_path");
        entry.tensor_count = read_json_number(entry_json, "tensor_count");
        entry.total_size_bytes = read_json_number(entry_json, "total_size_bytes");
        entry.loaded_timestamp = read_json_string(entry_json, "loaded_timestamp");
        entry.metadata_path = read_json_string(entry_json, "metadata_path");

        index_.push_back(entry);

        entry_start = entry_end + 1;
    }

    std::cout << "  [DEBUG] Loaded " << index_.size() << " models from index" << std::endl;
    return true;
}

bool WorkspaceMetadata::save_index() {
    std::ofstream file(index_path_);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"models\": [\n";

    for (size_t i = 0; i < index_.size(); i++) {
        const auto& entry = index_[i];

        file << "    {\n";
        file << "      \"name\": \"" << escape_json_string(entry.name) << "\",\n";
        file << "      \"quant_type\": \"" << escape_json_string(entry.quant_type) << "\",\n";
        file << "      \"gguf_path\": \"" << escape_json_string(entry.gguf_path) << "\",\n";
        file << "      \"tensor_count\": " << entry.tensor_count << ",\n";
        file << "      \"total_size_bytes\": " << entry.total_size_bytes << ",\n";
        file << "      \"loaded_timestamp\": \"" << escape_json_string(entry.loaded_timestamp) << "\",\n";
        file << "      \"metadata_path\": \"" << escape_json_string(entry.metadata_path) << "\"\n";
        file << "    }";

        if (i < index_.size() - 1) {
            file << ",";
        }
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    return file.good();
}

bool WorkspaceMetadata::create_directory_structure() {
    try {
        std::filesystem::create_directories(workspace_path_);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create directory structure: " << e.what() << std::endl;
        return false;
    }
}

std::string WorkspaceMetadata::get_model_dir(const std::string& model_name, const std::string& quant_type) const {
    return workspace_path_ + "/" + model_name + "/" + quant_type;
}

bool WorkspaceMetadata::load_model_json(const std::string& path, ModelMetadata& metadata) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    metadata.name = read_json_string(json, "name");
    metadata.gguf_path = read_json_string(json, "gguf_path");
    metadata.gguf_hash = read_json_string(json, "gguf_hash");
    metadata.quant_type = read_json_string(json, "quant_type");
    metadata.architecture = read_json_string(json, "architecture");
    metadata.tensor_count = read_json_number(json, "tensor_count");
    metadata.total_size_bytes = read_json_number(json, "total_size_bytes");
    metadata.vocab_size = read_json_number(json, "vocab_size");
    metadata.context_length = read_json_number(json, "context_length");
    metadata.embedding_length = read_json_number(json, "embedding_length");
    metadata.layer_count = read_json_number(json, "layer_count");
    metadata.loaded_timestamp = read_json_string(json, "loaded_timestamp");

    return true;
}

bool WorkspaceMetadata::save_model_json(const std::string& path, const ModelMetadata& metadata) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"name\": \"" << escape_json_string(metadata.name) << "\",\n";
    file << "  \"gguf_path\": \"" << escape_json_string(metadata.gguf_path) << "\",\n";
    file << "  \"gguf_hash\": \"" << escape_json_string(metadata.gguf_hash) << "\",\n";
    file << "  \"quant_type\": \"" << escape_json_string(metadata.quant_type) << "\",\n";
    file << "  \"architecture\": \"" << escape_json_string(metadata.architecture) << "\",\n";
    file << "  \"tensor_count\": " << metadata.tensor_count << ",\n";
    file << "  \"total_size_bytes\": " << metadata.total_size_bytes << ",\n";
    file << "  \"vocab_size\": " << metadata.vocab_size << ",\n";
    file << "  \"context_length\": " << metadata.context_length << ",\n";
    file << "  \"embedding_length\": " << metadata.embedding_length << ",\n";
    file << "  \"layer_count\": " << metadata.layer_count << ",\n";
    file << "  \"loaded_timestamp\": \"" << escape_json_string(metadata.loaded_timestamp) << "\"\n";
    file << "}\n";

    return file.good();
}

bool WorkspaceMetadata::load_tensors_json(const std::string& path, std::vector<TensorLocation>& tensors) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    tensors.clear();

    // Parse tensor array
    size_t pos = json.find("\"tensors\":[");
    if (pos == std::string::npos) {
        return true;  // Empty tensors
    }

    pos += 11;  // Skip "tensors":[
    size_t end = json.find(']', pos);
    if (end == std::string::npos) {
        return false;
    }

    std::string tensors_json = json.substr(pos, end - pos);

    // Parse each tensor entry
    size_t entry_start = 0;
    while ((entry_start = tensors_json.find('{', entry_start)) != std::string::npos) {
        size_t entry_end = tensors_json.find('}', entry_start);
        if (entry_end == std::string::npos) break;

        std::string entry_json = tensors_json.substr(entry_start, entry_end - entry_start + 1);

        TensorLocation tensor;
        tensor.name = read_json_string(entry_json, "name");
        tensor.vpid_offset = read_json_number(entry_json, "vpid_offset");
        tensor.size_bytes = read_json_number(entry_json, "size_bytes");
        tensor.element_count = read_json_number(entry_json, "element_count");
        tensor.original_type = read_json_string(entry_json, "original_type");
        tensor.dequant_type = read_json_string(entry_json, "dequant_type");

        tensors.push_back(tensor);

        entry_start = entry_end + 1;
    }

    return true;
}

bool WorkspaceMetadata::save_tensors_json(const std::string& path, const std::vector<TensorLocation>& tensors) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    file << "{\n";
    file << "  \"tensors\": [\n";

    for (size_t i = 0; i < tensors.size(); i++) {
        const auto& tensor = tensors[i];

        file << "    {\n";
        file << "      \"name\": \"" << escape_json_string(tensor.name) << "\",\n";
        file << "      \"vpid_offset\": " << tensor.vpid_offset << ",\n";
        file << "      \"size_bytes\": " << tensor.size_bytes << ",\n";
        file << "      \"element_count\": " << tensor.element_count << ",\n";
        file << "      \"original_type\": \"" << escape_json_string(tensor.original_type) << "\",\n";
        file << "      \"dequant_type\": \"" << escape_json_string(tensor.dequant_type) << "\"\n";
        file << "    }";

        if (i < tensors.size() - 1) {
            file << ",";
        }
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    return file.good();
}

} // namespace snapllm
