#include "snapllm/vpid_bridge.h"

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <shared_mutex>

using snapllm::model_lifecycle::budget_requires_eviction;
using snapllm::model_lifecycle::checked_model_size_mb;
using snapllm::model_lifecycle::ContextLifetimeTracker;
using snapllm::model_lifecycle::subtract_saturating;

int main() {
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };
    check(subtract_saturating(500, 200) == 300, "ordinary subtraction");
    check(subtract_saturating(200, 200) == 0, "equal subtraction");
    check(subtract_saturating(100, 200) == 0, "saturating subtraction");
    check(!budget_requires_eviction(6000, 2000, 0), "unlimited budget");
    check(!budget_requires_eviction(3000, 2000, 7000), "under budget");
    check(!budget_requires_eviction(5000, 2000, 7000), "exact budget");
    check(budget_requires_eviction(5001, 2000, 7000), "over budget");
    check(budget_requires_eviction(8000, 0, 7000), "already over budget");
    check(
        budget_requires_eviction(
            static_cast<size_t>(-2),
            8,
            static_cast<size_t>(-1)),
        "overflow-safe budget");
    const auto fixture = std::filesystem::temp_directory_path() /
        ("snapllm_model_size_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream output(fixture, std::ios::binary);
        output.put('x');
    }
    const auto tiny_size = checked_model_size_mb(fixture);
    check(tiny_size && *tiny_size == 1, "non-empty files round up to one MB");
    std::error_code cleanup_error;
    std::filesystem::remove(fixture, cleanup_error);
    check(!checked_model_size_mb(fixture), "missing model size fails closed");
    ContextLifetimeTracker lifetimes;
    auto lease = std::async(std::launch::async, [&lifetimes] {
        return lifetimes.acquire("model");
    }).get();
    auto unload = std::async(std::launch::async, [&lifetimes] {
        lifetimes.wait_for_zero("model");
        return true;
    });
    check(
        unload.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
        "model unload waits while a context lifetime lease exists");
    lease.reset();
    check(
        unload.wait_for(std::chrono::seconds(1)) == std::future_status::ready &&
            unload.get(),
        "model unload proceeds after context lifetime lease release");
    if (failures == 0) {
        std::cout << "model lifecycle accounting tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
