#include "snapllm/request_router.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

using namespace snapllm;

int main() {
    const std::vector<std::string> models{"chat-7b", "vision-vl"};
    const std::vector<ModelType> types{ModelType::TEXT_LLM, ModelType::MULTIMODAL_VL};

    auto text = RequestRouter::choose({"", "", "text"}, models, types, "chat-7b");
    assert(text.accepted && text.model == "chat-7b");

    auto vision = RequestRouter::choose({"", "", "vision"}, models, types, "chat-7b");
    assert(vision.accepted && vision.model == "vision-vl");

    auto wrong_route = RequestRouter::choose({"chat-7b", "", "vision"}, models, types, "chat-7b");
    assert(!wrong_route.accepted && wrong_route.error.find("does not support") != std::string::npos);

    auto missing = RequestRouter::choose({"missing", "", "text"}, models, types, "chat-7b");
    assert(!missing.accepted && missing.error.find("not loaded") != std::string::npos);

    auto task = RequestRouter::choose({"", "vision", "vision"}, models, types, "chat-7b");
    assert(task.accepted && task.model == "vision-vl");

    // Boundary/error matrix: routing must reject malformed registries and
    // unsupported modalities without mutating or guessing a model.
    auto inconsistent = RequestRouter::choose({"", "", "text"},
                                               {"chat-7b"},
                                               {},
                                               "chat-7b");
    assert(!inconsistent.accepted && inconsistent.error.find("inconsistent") != std::string::npos);

    auto unsupported = RequestRouter::choose({"", "", "audio"}, models, types, "chat-7b");
    assert(!unsupported.accepted && unsupported.error.find("Unsupported routing modality") != std::string::npos);

    auto empty_registry = RequestRouter::choose({"", "", "text"}, {}, {}, "");
    assert(!empty_registry.accepted && empty_registry.error.find("No loaded model") != std::string::npos);

    // RequestRouter is intentionally stateless. Exercise it concurrently to
    // guard against accidental shared routing state or data races.
    constexpr int thread_count = 8;
    constexpr int iterations = 1000;
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < iterations; ++i) {
                const auto decision = RequestRouter::choose({"", "", "vision"},
                                                            models, types, "chat-7b");
                if (!decision.accepted || decision.model != "vision-vl" ||
                    decision.type != ModelType::MULTIMODAL_VL) {
                    ++failures;
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    assert(failures.load() == 0);

    // Automatic routing is least-loaded, then lowest latency, with an
    // explicit round-robin cursor for exact ties.
    const std::vector<ModelLoad> loads{{4, 12.0}, {1, 30.0}};
    auto least_loaded = RequestRouter::choose({"", "", "text"}, models, types,
                                               "chat-7b", loads);
    assert(least_loaded.accepted && least_loaded.model == "chat-7b");

    const std::vector<std::string> text_models{"chat-a", "chat-b", "vision-vl"};
    const std::vector<ModelType> text_types{ModelType::TEXT_LLM, ModelType::TEXT_LLM,
                                            ModelType::MULTIMODAL_VL};
    const std::vector<ModelLoad> tied_loads{{2, 10.0}, {2, 10.0}, {0, 99.0}};
    auto tie_a = RequestRouter::choose({"", "", "text"}, text_models, text_types,
                                       "", tied_loads, 0);
    auto tie_b = RequestRouter::choose({"", "", "text"}, text_models, text_types,
                                       "", tied_loads, 1);
    assert(tie_a.accepted && tie_b.accepted && tie_a.model != tie_b.model);

    // Explicit pinning wins even when another model is idle.
    auto pinned = RequestRouter::choose({"chat-a", "", "text"}, text_models, text_types,
                                         "", {{99, 500.0}, {0, 1.0}, {0, 1.0}});
    assert(pinned.accepted && pinned.model == "chat-a");

    auto bad_loads = RequestRouter::choose({"", "", "text"}, models, types,
                                           "", {{0, 0.0}});
    assert(!bad_loads.accepted && bad_loads.error.find("inconsistent") != std::string::npos);

    std::cout << "request_router_test: PASS\n";
    return 0;
}
