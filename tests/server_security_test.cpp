#include "snapllm/server_security.h"

#include <chrono>
#include <filesystem>
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
            std::cout << "server_security_test: all checks passed\n";
            return 0;
        }
        std::cerr << "server_security_test: " << failures_ << " check(s) failed\n";
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
                ("snapllm_server_security_" + std::to_string(nonce));
        std::filesystem::create_directory(path_);
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

void test_loopback_hosts(TestRunner& runner) {
    using snapllm::security::is_loopback_host;
    using snapllm::security::is_valid_bind_host;
    using snapllm::security::is_valid_loopback_host_header;

    EXPECT(runner, is_loopback_host("localhost"));
    EXPECT(runner, is_loopback_host("LOCALHOST."));
    EXPECT(runner, is_loopback_host("127.0.0.1"));
    EXPECT(runner, is_loopback_host("127.255.12.34"));
    EXPECT(runner, is_loopback_host("::1"));
    EXPECT(runner, is_loopback_host("[::1]"));

    EXPECT(runner, !is_loopback_host(""));
    EXPECT(runner, !is_loopback_host("localhost:8080"));
    EXPECT(runner, !is_loopback_host("localhost.evil.example"));
    EXPECT(runner, !is_loopback_host("127.0.0.1.evil.example"));
    EXPECT(runner, !is_loopback_host("127.00.0.1"));
    EXPECT(runner, !is_loopback_host("128.0.0.1"));
    EXPECT(runner, !is_loopback_host("0.0.0.0"));
    EXPECT(runner, !is_loopback_host("localhost\r\nX-Injected: yes"));
    EXPECT(runner, !is_loopback_host("loc\xC3\xA1lhost"));

    EXPECT(runner, is_valid_bind_host("127.0.0.1"));
    EXPECT(runner, is_valid_bind_host("0.0.0.0"));
    EXPECT(runner, is_valid_bind_host("api.internal.example"));
    EXPECT(runner, is_valid_bind_host("::1"));
    EXPECT(runner, is_valid_bind_host("[::1]"));
    EXPECT(runner, !is_valid_bind_host(""));
    EXPECT(runner, !is_valid_bind_host("*"));
    EXPECT(runner, !is_valid_bind_host("-invalid.example"));
    EXPECT(runner, !is_valid_bind_host("invalid.example-"));
    EXPECT(runner, !is_valid_bind_host("localhost;calc.exe"));
    EXPECT(runner, !is_valid_bind_host("localhost/path"));
    EXPECT(runner, !is_valid_bind_host("localhost\r\nX-Injected: yes"));
    EXPECT(runner, !is_valid_bind_host("loc\xC3\xA1lhost"));

    EXPECT(runner, is_valid_loopback_host_header("localhost", 8080));
    EXPECT(runner, is_valid_loopback_host_header("localhost:8080", 8080));
    EXPECT(runner, is_valid_loopback_host_header("[::1]:8080", 8080));
    EXPECT(runner, !is_valid_loopback_host_header("localhost:8081", 8080));
    EXPECT(runner, !is_valid_loopback_host_header("evil.example:8080", 8080));
    EXPECT(runner, !is_valid_loopback_host_header("localhost:", 8080));
    EXPECT(runner, !is_valid_loopback_host_header("localhost:0", 8080));
    EXPECT(runner, !is_valid_loopback_host_header("localhost:99999", 8080));
    EXPECT(runner, !is_valid_loopback_host_header("localhost\n:8080", 8080));
}

void test_browser_origins(TestRunner& runner) {
    using snapllm::security::is_browser_origin_allowed;

    const std::vector<std::string> allowed{
        "http://localhost:8080",
        "tauri://localhost",
        "https://trusted.example",
    };

    EXPECT(runner, is_browser_origin_allowed("http://localhost:8080", allowed));
    EXPECT(runner, is_browser_origin_allowed("tauri://localhost", allowed));
    EXPECT(runner, !is_browser_origin_allowed("", allowed));
    EXPECT(runner, !is_browser_origin_allowed("null", allowed));
    EXPECT(runner, !is_browser_origin_allowed("http://localhost:8080.evil", allowed));
    EXPECT(runner, !is_browser_origin_allowed("http://localhost:8080/", allowed));
    EXPECT(runner, !is_browser_origin_allowed("http://localhost:", allowed));
    EXPECT(runner, !is_browser_origin_allowed("http://user@localhost:8080", allowed));
    EXPECT(runner, !is_browser_origin_allowed("http://*.example", allowed));
    EXPECT(runner, !is_browser_origin_allowed(
                       "http://localhost:8080\r\nX-Injected: yes", allowed));
    EXPECT(runner, !is_browser_origin_allowed("http://loc\xC3\xA1lhost:8080", allowed));

    const std::vector<std::string> malformed_allowlist{
        "http://trusted.example/path",
        "https://*.example",
    };
    EXPECT(runner, !is_browser_origin_allowed(
                       "http://trusted.example/path", malformed_allowlist));
}

void test_api_keys(TestRunner& runner) {
    using snapllm::security::bearer_api_key_matches;
    using snapllm::security::constant_time_equal;
    using snapllm::security::is_valid_network_guard;
    using snapllm::security::meets_api_key_policy;
    using snapllm::security::x_api_key_matches;

    const std::string key = "0123456789abcdef0123456789abcdef";
    const std::string other_key = "0123456789abcdef0123456789abcdeg";

    EXPECT(runner, meets_api_key_policy(key));
    EXPECT(runner, !meets_api_key_policy(""));
    EXPECT(runner, !meets_api_key_policy("short"));
    EXPECT(runner, !meets_api_key_policy(std::string(31, 'a')));
    EXPECT(runner, meets_api_key_policy(std::string(32, 'a')));
    EXPECT(runner, !meets_api_key_policy(std::string(4097, 'a')));
    EXPECT(runner, !meets_api_key_policy(std::string(31, 'a') + "\n"));
    EXPECT(runner, !meets_api_key_policy(std::string(31, 'a') + " "));
    EXPECT(runner, !meets_api_key_policy(std::string(31, 'a') + "\xC3\xA1"));
    EXPECT(runner, is_valid_network_guard("reverse-proxy"));
    EXPECT(runner, is_valid_network_guard("loopback-port-publish"));
    EXPECT(runner, !is_valid_network_guard(""));
    EXPECT(runner, !is_valid_network_guard("1"));
    EXPECT(runner, !is_valid_network_guard("REVERSE-PROXY"));
    EXPECT(runner, !is_valid_network_guard("loopback-port-publish "));

    EXPECT(runner, constant_time_equal("", ""));
    EXPECT(runner, constant_time_equal(key, key));
    EXPECT(runner, !constant_time_equal(key, other_key));
    EXPECT(runner, !constant_time_equal(key, key + "x"));

    EXPECT(runner, bearer_api_key_matches("Bearer " + key, key));
    EXPECT(runner, bearer_api_key_matches("bearer " + key, key));
    EXPECT(runner, !bearer_api_key_matches("", key));
    EXPECT(runner, !bearer_api_key_matches(key, key));
    EXPECT(runner, !bearer_api_key_matches("Bearer  " + key, key));
    EXPECT(runner, !bearer_api_key_matches("Bearer " + other_key, key));
    EXPECT(runner, !bearer_api_key_matches("Bearer " + key + "\r\n", key));
    EXPECT(runner, !bearer_api_key_matches("Bearer " + key, "short"));

    EXPECT(runner, x_api_key_matches(key, key));
    EXPECT(runner, !x_api_key_matches(other_key, key));
    EXPECT(runner, !x_api_key_matches("", key));
    EXPECT(runner, !x_api_key_matches(key + "\n", key));
    EXPECT(runner, !x_api_key_matches(key, "short"));
}

void test_path_containment(TestRunner& runner) {
    using snapllm::security::canonical_path_within_root;

    TemporaryDirectory temporary_directory;
    const std::filesystem::path root = temporary_directory.path() / "models";
    const std::filesystem::path sibling = temporary_directory.path() / "models-evil";
    std::filesystem::create_directories(root / "nested");
    std::filesystem::create_directories(sibling);

    const auto exact_root = canonical_path_within_root(root, root);
    EXPECT(runner, exact_root.has_value());

    const auto nested =
        canonical_path_within_root(root, root / "nested" / "model.gguf");
    EXPECT(runner, nested.has_value());
    EXPECT(runner, nested == std::filesystem::weakly_canonical(
                                 root / "nested" / "model.gguf"));

    EXPECT(runner, !canonical_path_within_root(
                        root, root / "nested" / ".." / ".." / "escape.gguf"));
    EXPECT(runner, !canonical_path_within_root(root, sibling / "model.gguf"));
    EXPECT(runner, !canonical_path_within_root(
                        root, std::filesystem::path{}));
    EXPECT(runner, !canonical_path_within_root(
                        root / "missing-root", root / "model.gguf"));

    EXPECT(runner, !canonical_path_within_root(
                        root, std::filesystem::path(R"(\\server\share\model.gguf)")));
    EXPECT(runner, !canonical_path_within_root(
                        root, std::filesystem::path(R"(\\?\C:\models\model.gguf)")));
    EXPECT(runner, !canonical_path_within_root(
                        root, std::filesystem::path(R"(\\.\C:\models\model.gguf)")));
    EXPECT(runner, !canonical_path_within_root(
                        root, std::filesystem::path("//server/share/model.gguf")));
    EXPECT(runner, !canonical_path_within_root(
                        root, std::filesystem::path("//?/C:/models/model.gguf")));
}

}  // namespace

int main() {
    TestRunner runner;
    test_loopback_hosts(runner);
    test_browser_origins(runner);
    test_api_keys(runner);
    test_path_containment(runner);
    return runner.finish();
}
