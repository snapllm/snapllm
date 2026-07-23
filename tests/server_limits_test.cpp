#include "snapllm/server_limits.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

class TestRunner {
public:
    void expect(bool condition, const char* expression, int line) {
        if (!condition) {
            ++failures_;
            std::cerr << "FAIL line " << line << ": " << expression << '\n';
        }
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "server_limits_test: all checks passed\n";
            return 0;
        }
        std::cerr << "server_limits_test: " << failures_ << " check(s) failed\n";
        return 1;
    }

private:
    int failures_ = 0;
};

#define EXPECT(runner, expression) (runner).expect((expression), #expression, __LINE__)

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("snapllm_server_limits_" + std::to_string(nonce));
        std::filesystem::create_directories(path_ / "models");
        std::filesystem::create_directories(path_ / "workspace");
        std::filesystem::create_directories(path_ / "outside");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void test_numeric_and_size_boundaries(TestRunner& runner) {
    using namespace snapllm::limits;

    EXPECT(runner, !is_valid_max_tokens(-1));
    EXPECT(runner, !is_valid_max_tokens(0));
    EXPECT(runner, is_valid_max_tokens(1));
    EXPECT(runner, is_valid_max_tokens(32768));
    EXPECT(runner, !is_valid_max_tokens(32769));

    EXPECT(runner, !is_valid_prompt_size(0));
    EXPECT(runner, is_valid_prompt_size(kMaximumPromptBytes));
    EXPECT(runner, !is_valid_prompt_size(kMaximumPromptBytes + 1));
    EXPECT(runner, is_valid_message_size(kMaximumMessageBytes));
    EXPECT(runner, !is_valid_message_size(kMaximumMessageBytes + 1));
    EXPECT(runner, is_valid_string_size(0));
    EXPECT(runner, !is_valid_string_size(kMaximumStringBytes + 1));

    EXPECT(runner, !is_valid_message_count(0));
    EXPECT(runner, is_valid_message_count(kMaximumMessages));
    EXPECT(runner, !is_valid_message_count(kMaximumMessages + 1));
    EXPECT(runner, !is_valid_batch_count(0));
    EXPECT(runner, is_valid_batch_count(kMaximumBatchItems));
    EXPECT(runner, !is_valid_batch_count(kMaximumBatchItems + 1));
    EXPECT(runner, is_valid_identifier_component("low-weight-ui-e2e"));
    EXPECT(runner, is_valid_identifier_component("model with spaces"));
    EXPECT(runner, !is_valid_identifier_component(""));
    EXPECT(runner, !is_valid_identifier_component("model/name"));
    EXPECT(runner, !is_valid_identifier_component("model\\name"));
    EXPECT(runner, !is_valid_identifier_component("model\nname"));
}

void test_image_boundaries(TestRunner& runner) {
    using namespace snapllm::limits;

    EXPECT(runner, !is_valid_diffusion_request(0, 512, 20));
    EXPECT(runner, !is_valid_diffusion_request(63, 512, 20));
    EXPECT(runner, is_valid_diffusion_request(64, 64, 1));
    EXPECT(runner, is_valid_diffusion_request(4096, 4096, 150));
    EXPECT(runner, !is_valid_diffusion_request(4097, 64, 20));
    EXPECT(runner, !is_valid_diffusion_request(4096, 4096, 151));
    EXPECT(runner, !is_valid_diffusion_request(4096, 4096, -1));

    EXPECT(runner, !is_valid_vision_image_count(0));
    EXPECT(runner, is_valid_vision_image_count(kMaximumVisionImages));
    EXPECT(runner, !is_valid_vision_image_count(kMaximumVisionImages + 1));
    EXPECT(runner, !is_valid_base64_image_size(0));
    EXPECT(runner, is_valid_base64_image_size(kMaximumBase64ImageBytes));
    EXPECT(runner, !is_valid_base64_image_size(kMaximumBase64ImageBytes + 1));
    EXPECT(runner, is_valid_decoded_image_dimensions(4096, 4096));
    EXPECT(runner, !is_valid_decoded_image_dimensions(-1, 1024));
    EXPECT(runner, !is_valid_decoded_image_dimensions(4097, 1024));

    const auto decoded = decode_base64_strict("aGVsbG8=");
    EXPECT(runner, decoded && std::string(decoded->begin(), decoded->end()) == "hello");
    EXPECT(runner, !decode_base64_strict("aGVsbG8").has_value());
    EXPECT(runner, !decode_base64_strict("a===YQ==").has_value());
    EXPECT(runner, !decode_base64_strict("YQ=A").has_value());
    EXPECT(runner, !decode_base64_strict("Y!Q=").has_value());
    EXPECT(runner, !decode_base64_strict(std::string("YQ") + char(0xff) + "=").has_value());
}

void test_last_role_index(TestRunner& runner) {
    using snapllm::limits::find_last_role_index;
    const std::vector<std::string_view> duplicate_users{
        "system", "user", "assistant", "user"};
    const auto index = find_last_role_index(duplicate_users, "user");
    EXPECT(runner, index.has_value());
    EXPECT(runner, index && *index == 3);
    EXPECT(runner, !find_last_role_index(duplicate_users, "tool").has_value());
}

void test_path_confinement(TestRunner& runner) {
    using snapllm::limits::canonical_path_within_roots;

    TemporaryDirectory temporary;
    const auto models = temporary.path() / "models";
    const auto workspace = temporary.path() / "workspace";
    const std::vector<std::filesystem::path> roots = {models, workspace};

    const auto relative = canonical_path_within_roots(roots, "nested/model.gguf");
    EXPECT(runner, relative.has_value());
    EXPECT(runner, relative && relative->parent_path().filename() == "nested");

    const auto workspace_file =
        canonical_path_within_roots(roots, workspace / "projector.gguf");
    EXPECT(runner, workspace_file.has_value());

    EXPECT(runner, !canonical_path_within_roots(
                        roots, temporary.path() / "outside" / "model.gguf"));
    EXPECT(runner, !canonical_path_within_roots(roots, "../outside/model.gguf"));
    EXPECT(runner, !canonical_path_within_roots(
                        roots, std::filesystem::path(R"(\\server\share\model.gguf)")));
    EXPECT(runner, !canonical_path_within_roots(
                        roots, std::filesystem::path(R"(\\?\C:\models\model.gguf)")));
}

void test_bounded_regular_file(TestRunner& runner) {
    using snapllm::limits::bounded_regular_file_size;
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "asset.png";
    {
        std::ofstream output(file, std::ios::binary);
        output << "png";
    }
    EXPECT(runner, bounded_regular_file_size(file, 3).value_or(0) == 3);
    EXPECT(runner, !bounded_regular_file_size(file, 2).has_value());
    EXPECT(runner, !bounded_regular_file_size(temporary.path(), 100).has_value());
    EXPECT(runner, !bounded_regular_file_size(
                        temporary.path() / "missing.png", 100).has_value());
}

}  // namespace

int main() {
    TestRunner runner;
    test_numeric_and_size_boundaries(runner);
    test_image_boundaries(runner);
    test_last_role_index(runner);
    test_path_confinement(runner);
    test_bounded_regular_file(runner);
    return runner.finish();
}
