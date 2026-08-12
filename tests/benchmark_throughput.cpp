#include "httplib.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>

// End-to-end benchmark: exercises the real HTTP listener and reports completed,
// failed, p50 and p95 latency. It is intentionally opt-in (never a ctest test).
int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 6930, requests = 32, concurrency = 8;
    std::string model;
    std::string mode = "unspecified";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](int& value) { if (i + 1 < argc) value = std::atoi(argv[++i]); };
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port") next(port);
        else if (arg == "--requests") next(requests);
        else if (arg == "--concurrency") next(concurrency);
        else if (arg == "--model" && i + 1 < argc) model = argv[++i];
        else if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
    }
    if (requests < 1 || concurrency < 1) return 2;
    httplib::Client probe(host, port);
    probe.set_connection_timeout(2, 0);
    probe.set_read_timeout(5, 0);
    const auto health = probe.Get("/health");
    if (!health || health->status != 200) {
        std::cerr << "benchmark.preflight_failed health="
                  << (health ? std::to_string(health->status) : "connection_failed") << "\n";
        return 3;
    }
    std::string scheduler = "unavailable";
    if (const auto config = probe.Get("/api/v1/config"); config && config->status == 200) {
        scheduler = config->body;
    }
    std::atomic<int> next_request{0}, completed{0}, failed{0};
    std::vector<double> latencies;
    std::mutex latency_mutex;
    const auto started = std::chrono::steady_clock::now();
    auto worker = [&] {
        // httplib::Client owns mutable connection state; use one client per
        // worker so the benchmark measures server concurrency rather than
        // introducing a client-side data race.
        httplib::Client client(host, port);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(600, 0);
        for (;;) {
            const int index = next_request.fetch_add(1);
            if (index >= requests) return;
            const auto begin = std::chrono::steady_clock::now();
            const std::string body = std::string("{\"model\":\"") + model +
                "\",\"messages\":[{\"role\":\"user\",\"content\":\"throughput probe\"}],\"max_tokens\":8}";
            auto response = client.Post("/v1/chat/completions", body, "application/json");
            const auto end = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - begin).count();
            std::lock_guard<std::mutex> lock(latency_mutex);
            latencies.push_back(ms);
            if (response && response->status >= 200 && response->status < 300) ++completed;
            else ++failed;
        }
    };
    std::vector<std::thread> workers;
    for (int i = 0; i < concurrency; ++i) workers.emplace_back(worker);
    for (auto& thread : workers) thread.join();
    std::sort(latencies.begin(), latencies.end());
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    auto percentile = [&](double p) { return latencies.empty() ? 0.0 : latencies[static_cast<size_t>(p * (latencies.size() - 1))]; };
    std::cout << "benchmark.mode " << mode << "\n"
              << "benchmark.server_health 200\n"
              << "benchmark.scheduler_config " << scheduler << "\n"
              << "throughput.requests " << requests << "\n"
              << "throughput.concurrency " << concurrency << "\n"
              << "throughput.completed " << completed.load() << "\n"
              << "throughput.failed " << failed.load() << "\n"
              << "throughput.rps " << (seconds > 0.0 ? completed.load() / seconds : 0.0) << "\n"
              << "latency.p50_ms " << percentile(0.50) << "\n"
              << "latency.p95_ms " << percentile(0.95) << "\n";
    return failed.load() == 0 ? 0 : 1;
}
