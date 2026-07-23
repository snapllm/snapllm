#include "snapllm/workspace_metadata.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main() {
    using snapllm::is_safe_workspace_component;
    using snapllm::is_path_within_workspace;

    if (!is_safe_workspace_component("model-Q4_K_M") ||
        is_safe_workspace_component("") ||
        is_safe_workspace_component(".") ||
        is_safe_workspace_component("..") ||
        is_safe_workspace_component("../outside") ||
        is_safe_workspace_component(R"(..\outside)") ||
        is_safe_workspace_component("C:drive") ||
        is_safe_workspace_component("line\nbreak")) {
        std::cerr << "workspace metadata component validation failed\n";
        return 1;
    }

    const auto root = std::filesystem::temp_directory_path() /
        ("snapllm_workspace_metadata_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    if (!is_path_within_workspace(root, root / "inside.json") ||
        is_path_within_workspace(root, root.parent_path() / "outside.json")) {
        std::cerr << "workspace canonical containment validation failed\n";
        return 1;
    }
    const auto link_target = std::filesystem::path(root.string() + "_link_target");
    std::filesystem::create_directories(link_target);
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(
        link_target, root / "linked", symlink_error);
    if (!symlink_error &&
        is_path_within_workspace(root, root / "linked" / "metadata.json")) {
        std::cerr << "workspace symlink escape was accepted\n";
        return 1;
    }
    if (!symlink_error) {
        snapllm::ModelMetadata escaped{};
        escaped.name = "linked";
        escaped.quant_type = "Q4_0";
        snapllm::WorkspaceMetadata symlink_writer(root.string());
        if (!symlink_writer.initialize() ||
            symlink_writer.save_model_metadata(escaped)) {
            std::cerr << "workspace symlink write escape was accepted\n";
            return 1;
        }
    }

    snapllm::ModelMetadata expected{};
    expected.name = "escaped-model";
    expected.quant_type = "Q4_0";
    expected.gguf_path = R"(C:\Models\a"b.gguf)";
    expected.gguf_hash = "hash";
    expected.architecture = "llama";
    expected.tensor_count = 1;
    expected.total_size_bytes = 16;
    expected.vocab_size = 32;
    expected.context_length = 128;
    expected.embedding_length = 8;
    expected.layer_count = 2;
    expected.loaded_timestamp = "2026-07-23T00:00:00Z";
    expected.tensors.push_back({
        R"(tensor\"name)", 1, 2, 3, "q4_0", "f32"});

    snapllm::WorkspaceMetadata writer(root.string());
    if (!writer.initialize() || !writer.save_model_metadata(expected)) {
        std::cerr << "workspace metadata save failed\n";
        return 1;
    }
    snapllm::WorkspaceMetadata reader(root.string());
    if (!reader.initialize()) {
        std::cerr << "workspace metadata reload failed\n";
        return 1;
    }
    const auto actual = reader.get_model_metadata(
        expected.name, expected.quant_type);
    if (actual.gguf_path != expected.gguf_path ||
        actual.tensors.size() != 1 ||
        actual.tensors[0].name != expected.tensors[0].name) {
        std::cerr << "escaped metadata did not round-trip\n";
        return 1;
    }

    const auto index_path = root / "index.json";
    std::ifstream index_before_stream(index_path, std::ios::binary);
    const std::string index_before{
        std::istreambuf_iterator<char>(index_before_stream), {}};
    index_before_stream.close();

    auto replacement = expected;
    replacement.gguf_path = R"(C:\Models\replacement.gguf)";
    replacement.gguf_hash = "replacement-hash";
    replacement.loaded_timestamp = "2026-07-23T00:01:00Z";
    replacement.tensors[0].name = "replacement.tensor";
    snapllm::set_workspace_atomic_write_failure_after_for_test(2);
    const bool failed_save = writer.save_model_metadata(replacement);
    snapllm::set_workspace_atomic_write_failure_for_test(false);
    snapllm::WorkspaceMetadata rollback_reader(root.string());
    const auto rolled_back = rollback_reader.initialize()
        ? rollback_reader.get_model_metadata(expected.name, expected.quant_type)
        : snapllm::ModelMetadata{};
    std::ifstream rollback_index_stream(index_path, std::ios::binary);
    const std::string rollback_index{
        std::istreambuf_iterator<char>(rollback_index_stream), {}};
    rollback_index_stream.close();
    if (failed_save || rollback_index != index_before ||
        rolled_back.gguf_path != expected.gguf_path ||
        rolled_back.gguf_hash != expected.gguf_hash ||
        rolled_back.tensors.size() != 1 ||
        rolled_back.tensors[0].name != expected.tensors[0].name) {
        std::cerr << "failed metadata transaction exposed mixed state\n";
        return 1;
    }

    snapllm::set_workspace_atomic_write_failure_for_test(true);
    const bool failed_remove = writer.remove_model(
        expected.name, expected.quant_type);
    snapllm::set_workspace_atomic_write_failure_for_test(false);
    std::ifstream index_after_stream(index_path, std::ios::binary);
    const std::string index_after{
        std::istreambuf_iterator<char>(index_after_stream), {}};
    index_after_stream.close();
    if (failed_remove || index_before != index_after ||
        !writer.model_exists(expected.name, expected.quant_type) ||
        !std::filesystem::exists(root / expected.name / expected.quant_type)) {
        std::cerr << "failed atomic removal did not preserve prior state\n";
        return 1;
    }
    const bool successful_remove = writer.remove_model(
        expected.name, expected.quant_type);
    const bool remains_in_memory = writer.model_exists(
        expected.name, expected.quant_type);
    const bool directory_remains =
        std::filesystem::exists(root / expected.name / expected.quant_type);
    if (!successful_remove || remains_in_memory || directory_remains) {
        std::cerr << "successful removal did not update index and directory\n";
        std::cerr << "  returned=" << successful_remove
                  << " in_memory=" << remains_in_memory
                  << " directory=" << directory_remains << '\n';
        return 1;
    }
    snapllm::WorkspaceMetadata removed_reader(root.string());
    if (!removed_reader.initialize() || removed_reader.get_model_count() != 0) {
        std::cerr << "removed model remained in persisted index\n";
        return 1;
    }

    const auto malformed_root = root.string() + "_malformed";
    std::filesystem::create_directories(malformed_root);
    {
        std::ofstream output(
            std::filesystem::path(malformed_root) / "index.json",
            std::ios::binary);
        output << R"({"version":1,"models":[{"name":3}]})";
    }
    snapllm::WorkspaceMetadata malformed(malformed_root);
    if (malformed.initialize()) {
        std::cerr << "malformed index was accepted\n";
        return 1;
    }

    const auto oversized_root = root.string() + "_oversized";
    std::filesystem::create_directories(oversized_root);
    {
        std::ofstream output(
            std::filesystem::path(oversized_root) / "index.json",
            std::ios::binary);
        output.seekp(16 * 1024 * 1024);
        output.put('x');
    }
    snapllm::WorkspaceMetadata oversized(oversized_root);
    if (oversized.initialize()) {
        std::cerr << "oversized index was accepted\n";
        return 1;
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::remove_all(link_target, cleanup_error);
    std::filesystem::remove_all(malformed_root, cleanup_error);
    std::filesystem::remove_all(oversized_root, cleanup_error);

    std::cout << "workspace_metadata_security: all checks passed\n";
    return 0;
}
