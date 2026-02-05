/**
 * @file validation.cpp
 * @brief Implementation of tensor validation system
 */

#include "snapllm/validation.h"
#include <algorithm>
#include <numeric>
#include <cstring>

namespace snapllm {

TensorStats TensorValidator::validate(
    const float* data,
    size_t count,
    const std::string& tensor_name,
    const std::string& stage)
{
    TensorStats stats;
    stats.tensor_name = tensor_name;
    stats.num_elements = count;

    if (!config_.enable_validation) {
        stats.is_valid = true;
        return stats;
    }

    if (!data || count == 0) {
        stats.is_valid = false;
        stats.error_message = "NULL data or zero count";
        if (config_.verbose_output) {
            std::cerr << "[VALIDATION ERROR] " << stage << " - " << tensor_name
                      << ": " << stats.error_message << std::endl;
        }
        return stats;
    }

    // Calculate statistics
    stats.min_value = data[0];
    stats.max_value = data[0];
    double sum = 0.0;
    double sum_sq = 0.0;

    for (size_t i = 0; i < count; i++) {
        float val = data[i];

        // Check for NaN/Inf
        if (std::isnan(val)) {
            stats.num_nans++;
        } else if (std::isinf(val)) {
            stats.num_infs++;
        } else {
            if (val == 0.0f) stats.num_zeros++;
            if (val < stats.min_value) stats.min_value = val;
            if (val > stats.max_value) stats.max_value = val;
            sum += val;
            sum_sq += val * val;
        }
    }

    // Calculate mean and std dev
    size_t valid_count = count - stats.num_nans - stats.num_infs;
    if (valid_count > 0) {
        stats.mean_value = static_cast<float>(sum / valid_count);
        double variance = (sum_sq / valid_count) - (stats.mean_value * stats.mean_value);
        stats.std_dev = static_cast<float>(std::sqrt(std::max(0.0, variance)));
    }

    // Validation checks
    if (stats.num_nans > 0) {
        stats.is_valid = false;
        stats.error_message = "Contains NaN values";
    } else if (stats.num_infs > 0) {
        stats.is_valid = false;
        stats.error_message = "Contains Inf values";
    } else if (stats.num_zeros == count) {
        stats.is_valid = false;
        stats.error_message = "All zeros";
    } else if (stats.min_value == stats.max_value && count > 1) {
        stats.is_valid = false;
        stats.error_message = "All values identical (constant tensor)";
    }

    if (config_.verbose_output) {
        print_stats(stats, stage);
        if (count <= 100 || !stats.is_valid) {
            print_samples(data, count, tensor_name);
        }
    }

    return stats;
}

bool TensorValidator::compare(
    const float* data1,
    const float* data2,
    size_t count,
    const std::string& tensor_name,
    float tolerance)
{
    if (!config_.enable_validation) {
        return true;
    }

    if (!data1 || !data2 || count == 0) {
        if (config_.verbose_output) {
            std::cerr << "[VALIDATION ERROR] Compare - " << tensor_name
                      << ": NULL data or zero count" << std::endl;
        }
        return false;
    }

    size_t mismatches = 0;
    float max_diff = 0.0f;
    size_t max_diff_idx = 0;

    for (size_t i = 0; i < count; i++) {
        float diff = std::abs(data1[i] - data2[i]);
        if (diff > tolerance) {
            mismatches++;
            if (diff > max_diff) {
                max_diff = diff;
                max_diff_idx = i;
            }
        }
    }

    bool match = (mismatches == 0);

    if (config_.verbose_output) {
        std::cout << "[VALIDATION] Compare - " << tensor_name << std::endl;
        std::cout << "  Elements: " << count << std::endl;
        std::cout << "  Tolerance: " << tolerance << std::endl;
        std::cout << "  Mismatches: " << mismatches << " ("
                  << (100.0 * mismatches / count) << "%)" << std::endl;
        if (mismatches > 0) {
            std::cout << "  Max diff: " << max_diff << " at index " << max_diff_idx << std::endl;
            std::cout << "  data1[" << max_diff_idx << "] = " << data1[max_diff_idx] << std::endl;
            std::cout << "  data2[" << max_diff_idx << "] = " << data2[max_diff_idx] << std::endl;
        }
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << std::endl;
    }

    return match;
}

void TensorValidator::print_stats(const TensorStats& stats, const std::string& stage)
{
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "[VALIDATION] " << stage << " - " << stats.tensor_name << std::endl;
    std::cout << "  Elements: " << stats.num_elements << std::endl;
    std::cout << "  Range: [" << stats.min_value << ", " << stats.max_value << "]" << std::endl;
    std::cout << "  Mean: " << stats.mean_value << std::endl;
    std::cout << "  Std Dev: " << stats.std_dev << std::endl;
    std::cout << "  Zeros: " << stats.num_zeros
              << " (" << (100.0 * stats.num_zeros / stats.num_elements) << "%)" << std::endl;

    if (stats.num_nans > 0) {
        std::cout << "  NaNs: " << stats.num_nans << " ⚠️" << std::endl;
    }
    if (stats.num_infs > 0) {
        std::cout << "  Infs: " << stats.num_infs << " ⚠️" << std::endl;
    }

    std::cout << "  Status: " << (stats.is_valid ? "✓ VALID" : "✗ INVALID") << std::endl;
    if (!stats.is_valid && !stats.error_message.empty()) {
        std::cout << "  Error: " << stats.error_message << std::endl;
    }
}

void TensorValidator::print_samples(
    const float* data,
    size_t count,
    const std::string& tensor_name)
{
    size_t samples = std::min(count, config_.max_samples);

    std::cout << "  First " << samples << " values: [";
    for (size_t i = 0; i < samples; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << data[i];
    }
    std::cout << "]" << std::endl;

    if (count > samples * 2) {
        std::cout << "  Last " << samples << " values: [";
        for (size_t i = count - samples; i < count; i++) {
            if (i > count - samples) std::cout << ", ";
            std::cout << data[i];
        }
        std::cout << "]" << std::endl;
    }
}

} // namespace snapllm
