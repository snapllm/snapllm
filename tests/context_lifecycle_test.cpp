#include "snapllm/context_manager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_shutdown_joins_owned_task() {
    snapllm::context_detail::BackgroundTaskGroup tasks;
    std::promise<void> started;
    std::promise<void> release;
    std::atomic<bool> completed{false};

    auto release_future = release.get_future().share();
    auto result = tasks.submit([&]() {
        started.set_value();
        release_future.wait();
        completed.store(true, std::memory_order_release);
        return 42;
    });

    started.get_future().wait();
    release.set_value();
    tasks.shutdown();

    expect(completed.load(std::memory_order_acquire),
           "shutdown must join manager-owned work");
    expect(result.get() == 42, "joined task must preserve its future result");
}

void test_content_hash_is_sha256() {
    expect(
        snapllm::ContextManager::compute_content_hash("abc") ==
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "content hashes must be canonical SHA-256");
    expect(
        snapllm::ContextManager::compute_content_hash("").size() == 64,
        "content hashes must contain 64 hexadecimal characters");
}

void test_task_failure_is_propagated() {
    snapllm::context_detail::BackgroundTaskGroup tasks;
    auto result = tasks.submit([]() -> int {
        throw std::runtime_error("expected");
    });
    tasks.shutdown();

    bool caught = false;
    try {
        static_cast<void>(result.get());
    } catch (const std::runtime_error&) {
        caught = true;
    }
    expect(caught, "task exceptions must be delivered through the future");
}

void test_shutdown_rejects_new_work() {
    snapllm::context_detail::BackgroundTaskGroup tasks;
    tasks.shutdown();

    bool rejected = false;
    try {
        static_cast<void>(tasks.submit([] {}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, "shutdown task groups must reject new work");
}

void test_query_failure_state_is_explicit() {
    snapllm::ContextQueryResult result;
    expect(result.ok(), "default query result should represent success");

    result.status = snapllm::ContextQueryResult::Status::ContextNotFound;
    result.error_message = "context not found";
    expect(!result.ok(), "typed query failure must not report success");
    expect(!result.error_message.empty(), "typed failure should carry context");
}

void test_cold_restore_promote_and_remove_accounting() {
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        fs::temp_directory_path() / ("snapllm-context-lifecycle-" + std::to_string(unique));
    const auto paths = snapllm::WorkspacePaths::from_home(root);
    for (const auto& directory : paths.get_required_directories()) {
        fs::create_directories(directory);
    }

    const std::string context_id = "ctx_restored";
    snapllm::KVCacheShape shape;
    shape.num_layers = 1;
    shape.num_heads = 1;
    shape.head_dim = 2;
    shape.sequence_length = 2;
    shape.dtype = snapllm::KVDataType::FP16;

    snapllm::KVCacheFileHeader header;
    header.set_context_id(context_id);
    header.set_model_id("model_test");
    header.set_shape(shape);
    header.data_size = shape.total_size();
    std::vector<std::byte> data(header.data_size, std::byte{0x2a});
    header.data_checksum =
        snapllm::kv_cache_detail::compute_checksum(data.data(), data.size());
    header.header_checksum =
        snapllm::kv_cache_detail::compute_header_checksum(header);

    {
        std::ofstream cache(
            paths.get_context_cache_path(context_id, "cold"),
            std::ios::binary | std::ios::trunc);
        cache.write(reinterpret_cast<const char*>(&header), sizeof(header));
        cache.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }
    {
        std::ofstream metadata(paths.get_context_metadata_path(context_id));
        metadata << "{\"context_id\":\"" << context_id
                 << "\",\"model_id\":\"model_test\",\"token_count\":2,"
                    "\"tier\":\"cold\",\"storage_size_bytes\":"
                 << header.data_size << "}";
    }

    {
        snapllm::ContextManager manager(nullptr, paths, nullptr);
        snapllm::ContextHandle handle;
        handle.id = context_id;
        handle.valid = true;
        const auto restored = manager.get_stats();
        expect(restored.total_contexts == 1 && restored.cold_contexts == 1,
               "restored context must start in the cold tier");
        expect(restored.cold_memory_bytes == header.data_size,
               "restored cold bytes must be accounted");

        expect(manager.promote(handle, "hot"),
               "cold context must load and promote to hot");
        const auto promoted = manager.get_stats();
        expect(promoted.hot_contexts == 1 && promoted.warm_contexts == 0 &&
                   promoted.cold_contexts == 0,
               "promotion must transfer tier counts exactly once");
        expect(promoted.hot_memory_bytes == header.data_size &&
                   promoted.warm_memory_bytes == 0 &&
                   promoted.cold_memory_bytes == 0 &&
                   promoted.total_memory_bytes == header.data_size,
               "promotion must transfer memory accounting without underflow");

        expect(manager.demote(handle, "cold"),
               "hot context must demote to cold tier");
        const auto demoted = manager.get_stats();
        expect(demoted.hot_contexts == 0,
               "demotion must release the hot context count");
        expect(demoted.cold_contexts == 1,
               "demotion must add one cold context");
        expect(demoted.total_memory_bytes == 0,
               "demotion must release resident memory");
        expect(fs::exists(paths.get_context_cache_path(context_id, "cold")),
               "demotion must persist the cache in the cold tier");
        expect(!fs::exists(paths.get_context_cache_path(context_id, "hot")),
               "demotion must remove the stale hot-tier cache");

        expect(manager.remove(handle), "restored context must be removable");
        const auto removed = manager.get_stats();
        expect(removed.total_contexts == 0 && removed.total_memory_bytes == 0 &&
                   removed.hot_memory_bytes == 0,
               "remove must release restored context accounting");
    }

    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    expect(!cleanup_error, "temporary lifecycle workspace cleanup");
}

void test_unknown_model_ingest_fails_without_allocation() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("snapllm-context-unknown-model-" + std::to_string(unique));
    const auto paths = snapllm::WorkspacePaths::from_home(root);
    for (const auto& directory : paths.get_required_directories()) {
        std::filesystem::create_directories(directory);
    }

    {
        snapllm::ContextManager manager(nullptr, paths, nullptr);
        snapllm::ContextSpec spec(std::string(1024 * 1024, 'x'), "missing-model");
        const auto handle = manager.ingest_sync(spec);
        expect(!handle.valid, "unknown-model ingest must fail closed");
        const auto stats = manager.get_stats();
        expect(stats.total_contexts == 0 && stats.total_memory_bytes == 0,
               "unknown-model ingest must not publish or allocate a context");
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    expect(!cleanup_error, "unknown-model temporary workspace cleanup");
}

} // namespace

int main() {
    test_content_hash_is_sha256();
    test_shutdown_joins_owned_task();
    test_task_failure_is_propagated();
    test_shutdown_rejects_new_work();
    test_query_failure_state_is_explicit();
    test_cold_restore_promote_and_remove_accounting();
    test_unknown_model_ingest_fails_without_allocation();

    if (failures != 0) {
        std::cerr << failures << " context lifecycle test(s) failed\n";
        return 1;
    }
    std::cout << "All context lifecycle tests passed\n";
    return 0;
}
