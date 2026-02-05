/**
 * @file benchmark_switching.cpp
 * @brief SnapLLM Model Switching Performance Benchmark Suite
 *
 * Measures and validates the <1ms model switching performance of the vPID architecture.
 *
 * Usage:
 *   benchmark_switching [--iterations N] [--models model1.gguf model2.gguf ...]
 *
 * Example:
 *   benchmark_switching --iterations 1000 --models D:\Models\medicine.gguf D:\Models\legal.gguf
 */

#include "snapllm/model_manager.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <iomanip>

using namespace snapllm;
using namespace std::chrono;

// ============================================================================
// Benchmark Results
// ============================================================================

struct BenchmarkResult {
    std::string test_name;
    int iterations;
    double min_ms;
    double max_ms;
    double mean_ms;
    double median_ms;
    double stddev_ms;
    double p95_ms;
    double p99_ms;
    bool passed;  // <1ms target
};

// ============================================================================
// Statistics Helpers
// ============================================================================

double calculate_mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double calculate_median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    if (n % 2 == 0) {
        return (values[n/2 - 1] + values[n/2]) / 2.0;
    }
    return values[n/2];
}

double calculate_stddev(const std::vector<double>& values, double mean) {
    if (values.size() < 2) return 0.0;
    double sum_sq = 0.0;
    for (double v : values) {
        sum_sq += (v - mean) * (v - mean);
    }
    return std::sqrt(sum_sq / (values.size() - 1));
}

double calculate_percentile(std::vector<double> values, double percentile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(percentile / 100.0 * (values.size() - 1));
    return values[index];
}

// ============================================================================
// Benchmark Functions
// ============================================================================

BenchmarkResult benchmark_model_switch(ModelManager& manager,
                                        const std::string& from_model,
                                        const std::string& to_model,
                                        int iterations) {
    BenchmarkResult result;
    result.test_name = "switch_" + from_model + "_to_" + to_model;
    result.iterations = iterations;

    std::vector<double> times_ms;
    times_ms.reserve(iterations);

    // Warm up
    manager.switch_model(from_model);
    manager.switch_model(to_model);
    manager.switch_model(from_model);

    // Benchmark
    for (int i = 0; i < iterations; i++) {
        // Ensure we're on from_model
        manager.switch_model(from_model);

        // Measure switch time
        auto start = high_resolution_clock::now();
        manager.switch_model(to_model);
        auto end = high_resolution_clock::now();

        double elapsed_ms = duration<double, std::milli>(end - start).count();
        times_ms.push_back(elapsed_ms);
    }

    // Calculate statistics
    result.min_ms = *std::min_element(times_ms.begin(), times_ms.end());
    result.max_ms = *std::max_element(times_ms.begin(), times_ms.end());
    result.mean_ms = calculate_mean(times_ms);
    result.median_ms = calculate_median(times_ms);
    result.stddev_ms = calculate_stddev(times_ms, result.mean_ms);
    result.p95_ms = calculate_percentile(times_ms, 95.0);
    result.p99_ms = calculate_percentile(times_ms, 99.0);
    result.passed = result.p99_ms < 1.0;  // <1ms at p99

    return result;
}

BenchmarkResult benchmark_rapid_switching(ModelManager& manager,
                                           const std::vector<std::string>& models,
                                           int iterations) {
    BenchmarkResult result;
    result.test_name = "rapid_switch_" + std::to_string(models.size()) + "_models";
    result.iterations = iterations;

    std::vector<double> times_ms;
    times_ms.reserve(iterations * models.size());

    // Warm up
    for (const auto& model : models) {
        manager.switch_model(model);
    }

    // Benchmark rapid switching through all models
    for (int i = 0; i < iterations; i++) {
        for (size_t j = 0; j < models.size(); j++) {
            auto start = high_resolution_clock::now();
            manager.switch_model(models[j]);
            auto end = high_resolution_clock::now();

            double elapsed_ms = duration<double, std::milli>(end - start).count();
            times_ms.push_back(elapsed_ms);
        }
    }

    // Calculate statistics
    result.min_ms = *std::min_element(times_ms.begin(), times_ms.end());
    result.max_ms = *std::max_element(times_ms.begin(), times_ms.end());
    result.mean_ms = calculate_mean(times_ms);
    result.median_ms = calculate_median(times_ms);
    result.stddev_ms = calculate_stddev(times_ms, result.mean_ms);
    result.p95_ms = calculate_percentile(times_ms, 95.0);
    result.p99_ms = calculate_percentile(times_ms, 99.0);
    result.passed = result.p99_ms < 1.0;

    return result;
}

BenchmarkResult benchmark_switch_under_load(ModelManager& manager,
                                             const std::string& from_model,
                                             const std::string& to_model,
                                             int iterations) {
    BenchmarkResult result;
    result.test_name = "switch_under_load";
    result.iterations = iterations;

    std::vector<double> times_ms;
    times_ms.reserve(iterations);

    // Warm up
    manager.switch_model(from_model);

    // Benchmark with simulated load (short inference before switch)
    for (int i = 0; i < iterations; i++) {
        manager.switch_model(from_model);

        // Simulate some work (short generation)
        manager.generate("Hello", 5);

        // Measure switch time
        auto start = high_resolution_clock::now();
        manager.switch_model(to_model);
        auto end = high_resolution_clock::now();

        double elapsed_ms = duration<double, std::milli>(end - start).count();
        times_ms.push_back(elapsed_ms);
    }

    // Calculate statistics
    result.min_ms = *std::min_element(times_ms.begin(), times_ms.end());
    result.max_ms = *std::max_element(times_ms.begin(), times_ms.end());
    result.mean_ms = calculate_mean(times_ms);
    result.median_ms = calculate_median(times_ms);
    result.stddev_ms = calculate_stddev(times_ms, result.mean_ms);
    result.p95_ms = calculate_percentile(times_ms, 95.0);
    result.p99_ms = calculate_percentile(times_ms, 99.0);
    result.passed = result.p99_ms < 1.0;

    return result;
}

// ============================================================================
// Report Printing
// ============================================================================

void print_result(const BenchmarkResult& result) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║ " << std::left << std::setw(60) << result.test_name << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Iterations: " << std::setw(48) << result.iterations << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "║ Min:    " << std::setw(10) << result.min_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "║ Max:    " << std::setw(10) << result.max_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "║ Mean:   " << std::setw(10) << result.mean_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "║ Median: " << std::setw(10) << result.median_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "║ StdDev: " << std::setw(10) << result.stddev_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "║ P95:    " << std::setw(10) << result.p95_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "║ P99:    " << std::setw(10) << result.p99_ms << " ms" << std::setw(39) << " " << " ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Target: <1ms at P99    Status: " << (result.passed ? "PASSED" : "FAILED")
              << std::setw(result.passed ? 22 : 22) << " " << " ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
}

void print_summary_ison(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n# Benchmark Summary (ISON Format)\n";
    std::cout << "# Generated: " << __DATE__ << " " << __TIME__ << "\n\n";

    std::cout << "benchmark.info\n";
    std::cout << "name \"SnapLLM Model Switching Performance\"\n";
    std::cout << "target_ms 1.0\n";
    std::cout << "total_tests " << results.size() << "\n\n";

    std::cout << "table.results\n";
    std::cout << "test_name iterations min_ms mean_ms median_ms p95_ms p99_ms passed\n";

    for (const auto& r : results) {
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "\"" << r.test_name << "\" "
                  << r.iterations << " "
                  << r.min_ms << " "
                  << r.mean_ms << " "
                  << r.median_ms << " "
                  << r.p95_ms << " "
                  << r.p99_ms << " "
                  << (r.passed ? "true" : "false") << "\n";
    }

    // Overall summary
    int passed = 0;
    for (const auto& r : results) {
        if (r.passed) passed++;
    }

    std::cout << "\nbenchmark.summary\n";
    std::cout << "total_passed " << passed << "\n";
    std::cout << "total_failed " << (results.size() - passed) << "\n";
    std::cout << "success_rate " << std::fixed << std::setprecision(2)
              << (100.0 * passed / results.size()) << "\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     SnapLLM Model Switching Performance Benchmark Suite      ║\n";
    std::cout << "║                    vPID Architecture Test                    ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // Parse arguments
    int iterations = 100;
    std::vector<std::string> model_paths;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if (arg == "--models") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                model_paths.push_back(argv[++i]);
            }
        } else if (arg == "--help") {
            std::cout << "Usage: benchmark_switching [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --iterations N     Number of iterations per test (default: 100)\n";
            std::cout << "  --models path...   Model GGUF files to load\n";
            std::cout << "  --help             Show this help\n";
            return 0;
        }
    }

    // Default models if none specified
    if (model_paths.empty()) {
        std::cout << "No models specified. Using default test models.\n";
        std::cout << "Specify models with: --models model1.gguf model2.gguf\n\n";

        // Try to find common model paths
        model_paths = {
            "D:\\Models\\medicine-llm.Q8_0.gguf",
            "D:\\Models\\legal.Q8_0.gguf"
        };
    }

    std::cout << "Configuration:\n";
    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Models: " << model_paths.size() << "\n";
    for (const auto& path : model_paths) {
        std::cout << "    - " << path << "\n";
    }
    std::cout << "\n";

    // Initialize model manager
    std::cout << "Initializing ModelManager...\n";
    ModelManager manager;

    // Load models
    std::vector<std::string> model_names;
    int model_idx = 0;
    for (const auto& path : model_paths) {
        std::string name = "model_" + std::to_string(model_idx++);
        std::cout << "Loading " << name << " from " << path << "...\n";

        if (!manager.load_model(name, path)) {
            std::cerr << "Failed to load model: " << path << "\n";
            continue;
        }
        model_names.push_back(name);
    }

    if (model_names.size() < 2) {
        std::cerr << "Need at least 2 models for switching benchmark.\n";
        return 1;
    }

    std::cout << "Loaded " << model_names.size() << " models successfully.\n\n";

    // Run benchmarks
    std::vector<BenchmarkResult> results;

    // Test 1: Basic switch between first two models
    std::cout << "Running: Basic Model Switch Test...\n";
    auto result1 = benchmark_model_switch(manager, model_names[0], model_names[1], iterations);
    print_result(result1);
    results.push_back(result1);

    // Test 2: Reverse switch
    std::cout << "Running: Reverse Switch Test...\n";
    auto result2 = benchmark_model_switch(manager, model_names[1], model_names[0], iterations);
    print_result(result2);
    results.push_back(result2);

    // Test 3: Rapid switching through all models
    if (model_names.size() >= 2) {
        std::cout << "Running: Rapid Multi-Model Switch Test...\n";
        auto result3 = benchmark_rapid_switching(manager, model_names, iterations);
        print_result(result3);
        results.push_back(result3);
    }

    // Test 4: Switch under load
    std::cout << "Running: Switch Under Load Test...\n";
    auto result4 = benchmark_switch_under_load(manager, model_names[0], model_names[1], iterations / 2);
    print_result(result4);
    results.push_back(result4);

    // Print ISON summary
    print_summary_ison(results);

    // Final verdict
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    bool all_passed = true;
    for (const auto& r : results) {
        if (!r.passed) all_passed = false;
    }
    if (all_passed) {
        std::cout << "║              BENCHMARK PASSED: <1ms SWITCHING               ║\n";
    } else {
        std::cout << "║          BENCHMARK FAILED: SOME TESTS EXCEEDED 1ms          ║\n";
    }
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";

    return all_passed ? 0 : 1;
}
