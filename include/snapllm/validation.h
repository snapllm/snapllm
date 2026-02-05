/**
 * @file validation.h
 * @brief Comprehensive validation system for tensor data integrity
 *
 * Validates tensor data at every stage:
 * - Post-dequantization
 * - Post-vPID write
 * - Post-vPID read
 * - Post-tensor wiring
 */

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace snapllm {

/**
 * @brief Validation configuration
 */
struct ValidationConfig {
    bool enable_validation = false;          // Master switch
    bool validate_dequantization = true;     // After dequantization
    bool validate_vpid_write = true;         // After writing to vPID
    bool validate_vpid_read = true;          // After reading from vPID
    bool validate_tensor_wiring = true;      // After wiring to llama.cpp
    bool verbose_output = true;              // Detailed validation logs
    size_t max_samples = 10;                 // Number of values to print
};

/**
 * @brief Tensor statistics for validation
 */
struct TensorStats {
    std::string tensor_name;
    size_t num_elements;
    float min_value;
    float max_value;
    float mean_value;
    float std_dev;
    size_t num_zeros;
    size_t num_nans;
    size_t num_infs;
    bool is_valid;
    std::string error_message;

    TensorStats() : num_elements(0), min_value(0), max_value(0), mean_value(0),
                    std_dev(0), num_zeros(0), num_nans(0), num_infs(0),
                    is_valid(true) {}
};

/**
 * @brief Tensor data validator
 */
class TensorValidator {
public:
    explicit TensorValidator(const ValidationConfig& config = ValidationConfig())
        : config_(config) {}

    /**
     * @brief Validate tensor data
     * @param data Tensor data
     * @param count Number of elements
     * @param tensor_name Tensor name for logging
     * @param stage Validation stage name
     * @return Statistics and validation result
     */
    TensorStats validate(
        const float* data,
        size_t count,
        const std::string& tensor_name,
        const std::string& stage = "unknown"
    );

    /**
     * @brief Compare two tensors for equality
     * @param data1 First tensor
     * @param data2 Second tensor
     * @param count Number of elements
     * @param tensor_name Tensor name for logging
     * @param tolerance Comparison tolerance
     * @return true if tensors match within tolerance
     */
    bool compare(
        const float* data1,
        const float* data2,
        size_t count,
        const std::string& tensor_name,
        float tolerance = 1e-6f
    );

    /**
     * @brief Set validation configuration
     */
    void set_config(const ValidationConfig& config) { config_ = config; }

    /**
     * @brief Get validation configuration
     */
    const ValidationConfig& get_config() const { return config_; }

    /**
     * @brief Enable/disable validation
     */
    void enable(bool enabled) { config_.enable_validation = enabled; }

    /**
     * @brief Check if validation is enabled
     */
    bool is_enabled() const { return config_.enable_validation; }

private:
    ValidationConfig config_;

    /**
     * @brief Print validation results
     */
    void print_stats(const TensorStats& stats, const std::string& stage);

    /**
     * @brief Print sample values
     */
    void print_samples(
        const float* data,
        size_t count,
        const std::string& tensor_name
    );
};

} // namespace snapllm
