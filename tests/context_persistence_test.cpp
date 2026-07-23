#include "snapllm/kv_cache.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

snapllm::KVCacheFileHeader valid_header() {
    snapllm::KVCacheFileHeader header;
    snapllm::KVCacheShape shape;
    shape.num_layers = 2;
    shape.num_heads = 2;
    shape.head_dim = 4;
    shape.sequence_length = 8;
    shape.dtype = snapllm::KVDataType::FP16;

    header.set_context_id("ctx_test");
    header.set_model_id("model_test");
    header.set_shape(shape);
    header.data_size = shape.total_size();
    header.data_checksum = 0x12345678U;
    header.header_checksum = snapllm::kv_cache_detail::compute_header_checksum(header);
    return header;
}

snapllm::kv_cache_detail::HeaderValidationError validate(
    const snapllm::KVCacheFileHeader& header,
    uint64_t file_size
) {
    size_t tensor_size = 0;
    return snapllm::kv_cache_detail::validate_file_header(header, file_size, tensor_size);
}

void test_valid_header_and_exact_file_size() {
    const auto header = valid_header();
    const uint64_t file_size = sizeof(header) + header.data_size;
    size_t tensor_size = 0;

    expect(
        snapllm::kv_cache_detail::validate_file_header(
            header, file_size, tensor_size) ==
            snapllm::kv_cache_detail::HeaderValidationError::None,
        "valid version-1 header should pass");
    expect(tensor_size == header.get_shape().layer_tensor_size(),
           "validated tensor size should match the shape");
    expect(validate(header, file_size - 1) ==
               snapllm::kv_cache_detail::HeaderValidationError::FileSizeMismatch,
           "truncated files should fail");
    expect(validate(header, file_size + 1) ==
               snapllm::kv_cache_detail::HeaderValidationError::FileSizeMismatch,
           "files with trailing data should fail");
}

void test_header_rejects_unsupported_and_corrupt_fields() {
    const auto original = valid_header();
    const uint64_t file_size = sizeof(original) + original.data_size;

    auto header = original;
    header.magic[0] = 'X';
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::InvalidMagic,
           "invalid magic should fail");

    header = original;
    header.version = 2;
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::UnsupportedVersion,
           "unsupported versions should fail");

    header = original;
    header.flags = snapllm::KVCacheFileHeader::FLAG_COMPRESSED;
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::UnsupportedFlags,
           "unimplemented compression should fail closed");

    header = original;
    header.dtype = (std::numeric_limits<uint32_t>::max)();
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::UnsupportedDataType,
           "unsupported dtypes should fail");

    header = original;
    header.num_layers = 0;
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::InvalidDimensions,
           "zero dimensions should fail");

    header = original;
    header.num_layers = snapllm::kv_cache_detail::MAX_KVC_LAYERS + 1;
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::DimensionLimitExceeded,
           "excessive layer counts should fail before allocation");

    header = original;
    ++header.data_size;
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::DataSizeMismatch,
           "shape/data-size disagreement should fail");

    header = original;
    header.header_checksum ^= 1U;
    expect(validate(header, file_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::HeaderChecksumMismatch,
           "header corruption should fail checksum validation");
}

void test_checked_shape_arithmetic_and_data_limit() {
    snapllm::KVCacheShape overflow_shape;
    overflow_shape.num_layers = (std::numeric_limits<uint32_t>::max)();
    overflow_shape.num_heads = (std::numeric_limits<uint32_t>::max)();
    overflow_shape.head_dim = (std::numeric_limits<uint32_t>::max)();
    overflow_shape.sequence_length = (std::numeric_limits<uint32_t>::max)();
    overflow_shape.dtype = snapllm::KVDataType::FP32;
    expect(overflow_shape.layer_tensor_size() == 0,
           "overflowing tensor multiplication should fail");
    expect(overflow_shape.total_size() == 0,
           "overflowing total-size multiplication should fail");

    snapllm::KVCacheShape int4_shape;
    int4_shape.num_layers = 1;
    int4_shape.num_heads = 1;
    int4_shape.head_dim = 3;
    int4_shape.sequence_length = 1;
    int4_shape.dtype = snapllm::KVDataType::INT4;
    expect(int4_shape.layer_tensor_size() == 2,
           "odd INT4 element counts should round up without overflow");

    snapllm::KVCacheShape large_shape;
    large_shape.num_layers = snapllm::kv_cache_detail::MAX_KVC_LAYERS;
    large_shape.num_heads = 64;
    large_shape.head_dim = 128;
    large_shape.sequence_length = 4096;
    large_shape.dtype = snapllm::KVDataType::FP32;

    snapllm::KVCacheFileHeader header;
    header.set_context_id("ctx_large");
    header.set_model_id("model_large");
    header.set_shape(large_shape);
    header.data_size = large_shape.total_size();
    header.header_checksum = snapllm::kv_cache_detail::compute_header_checksum(header);
    expect(validate(header, sizeof(header) + header.data_size) ==
               snapllm::kv_cache_detail::HeaderValidationError::DataSizeLimitExceeded,
           "cache data above the configured limit should fail before allocation");
}

void test_bounded_strings_and_incremental_checksum() {
    snapllm::KVCacheFileHeader header;
    std::memset(header.context_id, 'a', sizeof(header.context_id));
    std::memset(header.model_id, 'b', sizeof(header.model_id));
    expect(header.get_context_id().size() == sizeof(header.context_id),
           "unterminated context IDs should be extracted within the array");
    expect(header.get_model_id().size() == sizeof(header.model_id),
           "unterminated model IDs should be extracted within the array");

    const std::string first = "cache";
    const std::string second = "-payload";
    const std::string combined = first + second;
    snapllm::kv_cache_detail::CRC32 incremental;
    incremental.update(first.data(), first.size());
    incremental.update(second.data(), second.size());
    expect(incremental.value() ==
               snapllm::kv_cache_detail::compute_checksum(combined.data(), combined.size()),
           "incremental checksum should match contiguous checksum");
}

} // namespace

int main() {
    static_assert(sizeof(snapllm::KVCacheFileHeader) == 256,
                  "the version-1 on-disk header size must remain unchanged");

    test_valid_header_and_exact_file_size();
    test_header_rejects_unsupported_and_corrupt_fields();
    test_checked_shape_arithmetic_and_data_limit();
    test_bounded_strings_and_incremental_checksum();

    if (failures != 0) {
        std::cerr << failures << " context persistence test(s) failed\n";
        return 1;
    }
    std::cout << "All context persistence tests passed\n";
    return 0;
}
