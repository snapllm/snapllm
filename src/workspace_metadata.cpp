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
#include <chrono>
#include "json.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr uintmax_t kMaximumWorkspaceJsonBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaximumMetadataStringBytes = 64 * 1024;
using json = nlohmann::json;
#ifdef SNAPLLM_WORKSPACE_TEST_HOOKS
int atomic_writes_until_failure = -1;
#endif

bool is_bounded_regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) &&
           !error &&
           std::filesystem::file_size(path, error) <= kMaximumWorkspaceJsonBytes &&
           !error;
}

bool is_bounded_string(const std::string& value) {
    return value.size() <= kMaximumMetadataStringBytes;
}

bool is_safe_metadata_path(
    const std::filesystem::path& relative,
    const std::string& model_name,
    const std::string& quant_type) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.lexically_normal() != relative ||
        relative.filename() != "metadata.json") {
        return false;
    }
    auto component = relative.begin();
    if (component == relative.end() || component->string() != model_name) return false;
    ++component;
    if (component == relative.end() || component->string() != quant_type) return false;
    for (; component != relative.end(); ++component) {
        const auto value = component->string();
        if (value.empty() || value == "." || value == "..") return false;
    }
    return true;
}

bool atomic_write_text(
    const std::filesystem::path& destination,
    const std::string& contents) {
#ifdef SNAPLLM_WORKSPACE_TEST_HOOKS
    if (atomic_writes_until_failure == 0) return false;
    if (atomic_writes_until_failure > 0) --atomic_writes_until_failure;
#endif
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = destination;
    temporary += ".tmp." + std::to_string(nonce);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        file.flush();
        if (!file) {
            file.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
#endif
    return true;
}

} // anonymous namespace

namespace snapllm {

#ifdef SNAPLLM_WORKSPACE_TEST_HOOKS
void set_workspace_atomic_write_failure_for_test(bool enabled) noexcept {
    atomic_writes_until_failure = enabled ? 0 : -1;
}

void set_workspace_atomic_write_failure_after_for_test(
    int successful_writes) noexcept {
    atomic_writes_until_failure = successful_writes;
}
#endif

bool is_safe_workspace_component(const std::string& value) noexcept {
    if (value.empty() || value.size() > 255 || value == "." || value == "..") {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20U && character != '/' && character != '\\' &&
               character != ':' && character != '\0';
    });
}

bool is_path_within_workspace(
    const std::filesystem::path& workspace,
    const std::filesystem::path& candidate) noexcept {
    std::error_code error;
    const auto canonical_workspace =
        std::filesystem::weakly_canonical(workspace, error);
    if (error) return false;
    const auto canonical_candidate =
        std::filesystem::weakly_canonical(candidate, error);
    if (error) return false;
    const auto relative =
        canonical_candidate.lexically_relative(canonical_workspace);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && first->string() != "..";
}

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

    // A present-but-invalid index is an error; never overwrite it silently.
    std::error_code index_error;
    const bool index_exists = std::filesystem::exists(index_path_, index_error);
    if (index_error) return false;
    if (index_exists) {
        if (!load_index()) return false;
    } else {
        std::cout << "Creating new workspace index" << std::endl;
        index_.clear();
        if (!save_index()) return false;
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
    if (!is_safe_workspace_component(model_name) ||
        !is_safe_workspace_component(quant_type)) {
        return metadata;
    }

    // Find entry in index
    for (const auto& entry : index_) {
        if (entry.name == model_name && entry.quant_type == quant_type) {
            const std::filesystem::path relative_metadata(entry.metadata_path);
            if (!is_safe_metadata_path(relative_metadata, model_name, quant_type)) {
                return metadata;
            }
            const auto metadata_path =
                std::filesystem::path(workspace_path_) / relative_metadata;
            const auto tensors_path = metadata_path.parent_path() / "tensors.json";

            if (!is_path_within_workspace(workspace_path_, metadata_path) ||
                !is_path_within_workspace(workspace_path_, tensors_path) ||
                !load_model_json(metadata_path.string(), metadata)) {
                std::cerr << "Failed to load model metadata from " << metadata_path.string() << std::endl;
                return metadata;
            }

            if (!load_tensors_json(tensors_path.string(), metadata.tensors)) {
                std::cerr << "Failed to load tensor metadata from " << tensors_path.string() << std::endl;
                return metadata;
            }

            return metadata;
        }
    }

    return metadata;
}

bool WorkspaceMetadata::save_model_metadata(const ModelMetadata& metadata) {
    if (!is_safe_workspace_component(metadata.name) ||
        !is_safe_workspace_component(metadata.quant_type)) {
        std::cerr << "Refusing unsafe model metadata path components" << std::endl;
        return false;
    }

    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto relative_generation =
        std::filesystem::path(metadata.name) / metadata.quant_type /
        "generations" / std::to_string(nonce);
    const auto generation_dir =
        std::filesystem::path(workspace_path_) / relative_generation;
    if (!is_path_within_workspace(workspace_path_, generation_dir)) {
        std::cerr << "Refusing metadata generation outside workspace" << std::endl;
        return false;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(generation_dir, directory_error);
    if (directory_error) return false;

    const auto metadata_path = generation_dir / "metadata.json";
    const auto tensors_path = generation_dir / "tensors.json";
    if (!is_path_within_workspace(workspace_path_, metadata_path) ||
        !is_path_within_workspace(workspace_path_, tensors_path)) {
        std::cerr << "Refusing metadata files outside workspace" << std::endl;
        return false;
    }
    if (!save_model_json(metadata_path.string(), metadata)) {
        std::cerr << "Failed to save model metadata" << std::endl;
        std::filesystem::remove_all(generation_dir, directory_error);
        return false;
    }

    if (!save_tensors_json(tensors_path.string(), metadata.tensors)) {
        std::cerr << "Failed to save tensor metadata" << std::endl;
        std::filesystem::remove_all(generation_dir, directory_error);
        return false;
    }

    const auto previous_index = index_;
    std::filesystem::path previous_metadata_path;
    bool found = false;
    for (auto& entry : index_) {
        if (entry.name == metadata.name && entry.quant_type == metadata.quant_type) {
            previous_metadata_path = entry.metadata_path;
            entry.tensor_count = metadata.tensor_count;
            entry.total_size_bytes = metadata.total_size_bytes;
            entry.loaded_timestamp = metadata.loaded_timestamp;
            entry.gguf_path = metadata.gguf_path;
            entry.metadata_path =
                (relative_generation / "metadata.json").generic_string();
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
        entry.metadata_path =
            (relative_generation / "metadata.json").generic_string();
        index_.push_back(entry);
    }

    if (!save_index()) {
        index_ = previous_index;
        std::filesystem::remove_all(generation_dir, directory_error);
        return false;
    }

    if (!previous_metadata_path.empty()) {
        const auto previous_generation =
            (std::filesystem::path(workspace_path_) / previous_metadata_path).parent_path();
        if (previous_generation.parent_path().filename() == "generations" &&
            previous_generation != generation_dir) {
            std::filesystem::remove_all(previous_generation, directory_error);
        }
    }
    return true;
}

bool WorkspaceMetadata::remove_model(const std::string& model_name, const std::string& quant_type) {
    if (!is_safe_workspace_component(model_name) ||
        !is_safe_workspace_component(quant_type)) {
        return false;
    }

    const auto previous_index = index_;
    // Remove from index
    auto it = std::remove_if(index_.begin(), index_.end(),
        [&](const WorkspaceIndexEntry& entry) {
            return entry.name == model_name && entry.quant_type == quant_type;
        });

    if (it == index_.end()) {
        return false;  // Not found
    }

    index_.erase(it, index_.end());
    if (!save_index()) {
        index_ = previous_index;
        return false;
    }

    // Persist removal before cleanup so a failed index write never destroys
    // the only copy of model data.
    std::error_code remove_error;
    std::filesystem::remove_all(get_model_dir(model_name, quant_type), remove_error);
    if (remove_error) {
        std::cerr << "Failed to clean removed model directory: "
                  << remove_error.message() << std::endl;
        return false;
    }
    return true;
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
    std::transform(
        upper_filename.begin(), upper_filename.end(), upper_filename.begin(),
        [](unsigned char character) { return static_cast<char>(std::toupper(character)); });

    for (const auto& pattern : patterns) {
        // Check if pattern exists in uppercase filename
        std::string upper_pattern = pattern;
        std::transform(
            upper_pattern.begin(), upper_pattern.end(), upper_pattern.begin(),
            [](unsigned char character) { return static_cast<char>(std::toupper(character)); });

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
    if (!is_path_within_workspace(workspace_path_, index_path_) ||
        !is_bounded_regular_file(index_path_)) {
        return false;
    }
    std::ifstream file(index_path_);
    if (!file) return false;
    try {
        const auto root = json::parse(file);
        if (!root.is_object() || root.value("version", 0) != 1 ||
            !root.contains("models") || !root["models"].is_array()) {
            return false;
        }
        std::vector<WorkspaceIndexEntry> parsed;
        parsed.reserve(root["models"].size());
        for (const auto& item : root["models"]) {
            if (!item.is_object()) return false;
            WorkspaceIndexEntry entry{
                item.at("name").get<std::string>(),
                item.at("quant_type").get<std::string>(),
                item.at("gguf_path").get<std::string>(),
                item.at("tensor_count").get<std::size_t>(),
                item.at("total_size_bytes").get<std::size_t>(),
                item.at("loaded_timestamp").get<std::string>(),
                item.at("metadata_path").get<std::string>()};
            if (!is_safe_workspace_component(entry.name) ||
                !is_safe_workspace_component(entry.quant_type) ||
                !is_bounded_string(entry.gguf_path) ||
                !is_bounded_string(entry.loaded_timestamp) ||
                !is_bounded_string(entry.metadata_path) ||
                !is_safe_metadata_path(
                    std::filesystem::path(entry.metadata_path),
                    entry.name,
                    entry.quant_type)) {
                return false;
            }
            parsed.push_back(std::move(entry));
        }
        index_ = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool WorkspaceMetadata::save_index() {
    json models = json::array();
    for (const auto& entry : index_) {
        models.push_back({
            {"name", entry.name},
            {"quant_type", entry.quant_type},
            {"gguf_path", entry.gguf_path},
            {"tensor_count", entry.tensor_count},
            {"total_size_bytes", entry.total_size_bytes},
            {"loaded_timestamp", entry.loaded_timestamp},
            {"metadata_path", entry.metadata_path}});
    }
    return atomic_write_text(
        index_path_, json{{"version", 1}, {"models", std::move(models)}}.dump(2) + "\n");
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
    if (!is_bounded_regular_file(path)) {
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        const auto value = json::parse(file);
        ModelMetadata parsed;
        parsed.name = value.at("name").get<std::string>();
        parsed.gguf_path = value.at("gguf_path").get<std::string>();
        parsed.gguf_hash = value.at("gguf_hash").get<std::string>();
        parsed.quant_type = value.at("quant_type").get<std::string>();
        parsed.architecture = value.at("architecture").get<std::string>();
        parsed.tensor_count = value.at("tensor_count").get<std::size_t>();
        parsed.total_size_bytes = value.at("total_size_bytes").get<std::size_t>();
        parsed.vocab_size = value.at("vocab_size").get<std::size_t>();
        parsed.context_length = value.at("context_length").get<std::size_t>();
        parsed.embedding_length = value.at("embedding_length").get<std::size_t>();
        parsed.layer_count = value.at("layer_count").get<std::size_t>();
        parsed.loaded_timestamp = value.at("loaded_timestamp").get<std::string>();
        if (!is_safe_workspace_component(parsed.name) ||
            !is_safe_workspace_component(parsed.quant_type) ||
            !is_bounded_string(parsed.gguf_path) ||
            !is_bounded_string(parsed.gguf_hash) ||
            !is_bounded_string(parsed.architecture) ||
            !is_bounded_string(parsed.loaded_timestamp)) {
            return false;
        }
        metadata = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool WorkspaceMetadata::save_model_json(const std::string& path, const ModelMetadata& metadata) {
    return atomic_write_text(path, json{
        {"name", metadata.name},
        {"gguf_path", metadata.gguf_path},
        {"gguf_hash", metadata.gguf_hash},
        {"quant_type", metadata.quant_type},
        {"architecture", metadata.architecture},
        {"tensor_count", metadata.tensor_count},
        {"total_size_bytes", metadata.total_size_bytes},
        {"vocab_size", metadata.vocab_size},
        {"context_length", metadata.context_length},
        {"embedding_length", metadata.embedding_length},
        {"layer_count", metadata.layer_count},
        {"loaded_timestamp", metadata.loaded_timestamp}}.dump(2) + "\n");
}

bool WorkspaceMetadata::load_tensors_json(const std::string& path, std::vector<TensorLocation>& tensors) const {
    if (!is_bounded_regular_file(path)) {
        return false;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        const auto value = json::parse(file);
        if (!value.is_object() || !value.contains("tensors") ||
            !value["tensors"].is_array()) return false;
        std::vector<TensorLocation> parsed;
        parsed.reserve(value["tensors"].size());
        for (const auto& item : value["tensors"]) {
            TensorLocation tensor{
                item.at("name").get<std::string>(),
                item.at("vpid_offset").get<std::size_t>(),
                item.at("size_bytes").get<std::size_t>(),
                item.at("element_count").get<std::size_t>(),
                item.at("original_type").get<std::string>(),
                item.at("dequant_type").get<std::string>()};
            if (!is_bounded_string(tensor.name) ||
                !is_bounded_string(tensor.original_type) ||
                !is_bounded_string(tensor.dequant_type)) return false;
            parsed.push_back(std::move(tensor));
        }
        tensors = std::move(parsed);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool WorkspaceMetadata::save_tensors_json(const std::string& path, const std::vector<TensorLocation>& tensors) {
    json values = json::array();
    for (const auto& tensor : tensors) {
        values.push_back({
            {"name", tensor.name},
            {"vpid_offset", tensor.vpid_offset},
            {"size_bytes", tensor.size_bytes},
            {"element_count", tensor.element_count},
            {"original_type", tensor.original_type},
            {"dequant_type", tensor.dequant_type}});
    }
    return atomic_write_text(
        path, json{{"tensors", std::move(values)}}.dump(2) + "\n");
}

} // namespace snapllm
