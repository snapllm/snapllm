/**
 * @file file_cache_store.cpp
 * @brief File-based KV Cache Store Implementation
 *
 * Implements filesystem-based persistent storage for KV caches.
 * Part of vPID L2 context management system.
 */

#include "snapllm/file_cache_store.h"
#include "snapllm/compression.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iomanip>

namespace snapllm {

//=============================================================================
// Constructor / Destructor
//=============================================================================

FileCacheStore::FileCacheStore(const fs::path& path, size_t capacity)
    : store_path_(path)
    , capacity_bytes_(capacity)
    , used_bytes_(0)
{
    // Create directory if it doesn't exist
    std::error_code ec;
    if (!fs::exists(store_path_)) {
        fs::create_directories(store_path_, ec);
        if (ec) {
            std::cerr << "[FileCacheStore] Failed to create directory: "
                      << store_path_.string() << " - " << ec.message() << std::endl;
        }
    }

    // Rebuild index from existing files
    rebuild_index();

    std::cout << "[FileCacheStore] Initialized at " << store_path_.string()
              << " (" << metadata_cache_.size() << " entries, "
              << (used_bytes_ / (1024 * 1024)) << " MB used)" << std::endl;
}

FileCacheStore::~FileCacheStore() {
    sync();
}

//=============================================================================
// Core Operations
//=============================================================================

CacheWriteResult FileCacheStore::write(
    const std::string& cache_id,
    const void* data,
    size_t size,
    const CacheEntryInfo& info,
    const CacheWriteOptions& options
) {
    CacheWriteResult result;
    auto start = std::chrono::high_resolution_clock::now();

    // Check capacity
    if (capacity_bytes_ > 0 && used_bytes_ + size > capacity_bytes_) {
        result.success = false;
        result.error_message = "Insufficient capacity";
        return result;
    }

    // Prepare paths
    auto cache_path = get_cache_file_path(cache_id);
    auto temp_path = cache_path;
    temp_path += ".tmp";

    // Write to temp file first (atomic write pattern)
    {
        std::ofstream file(temp_path, std::ios::binary);
        if (!file) {
            result.success = false;
            result.error_message = "Failed to create temp file";
            return result;
        }

        // Apply compression if requested
        if (options.compress) {
            Compressor compressor;
            CompressionConfig config = CompressionConfig::lz4();
            if (options.compression_level > 5) {
                config = CompressionConfig::zstd(options.compression_level);
            } else if (options.compression_level > 1) {
                config = CompressionConfig::lz4_hc(options.compression_level);
            }

            auto comp_result = compressor.compress(data, size, config);
            if (!comp_result.success) {
                result.success = false;
                result.error_message = "Compression failed: " + comp_result.error_message;
                std::error_code ec;
                fs::remove(temp_path, ec);
                return result;
            }

            file.write(reinterpret_cast<const char*>(comp_result.data.data()),
                       comp_result.data.size());

            // Log compression ratio
            std::cout << "[FileCacheStore] Compressed " << (size / 1024) << " KB -> "
                      << (comp_result.data.size() / 1024) << " KB ("
                      << std::fixed << std::setprecision(1) << comp_result.ratio() << "x)"
                      << std::endl;
        } else {
            file.write(static_cast<const char*>(data), size);
        }

        if (!file.good()) {
            result.success = false;
            result.error_message = "Write failed";
            std::error_code ec;
            fs::remove(temp_path, ec);
            return result;
        }
    }

    // Compute checksum if requested
    if (options.verify_checksum) {
        result.checksum = compute_file_checksum(temp_path);
    }

    // Atomic rename
    std::error_code ec;
    fs::rename(temp_path, cache_path, ec);
    if (ec) {
        result.success = false;
        result.error_message = "Rename failed: " + ec.message();
        fs::remove(temp_path, ec);
        return result;
    }

    // Write metadata file
    CacheEntryInfo stored_info = info;
    stored_info.cache_id = cache_id;
    stored_info.size_bytes = size;
    stored_info.checksum = result.checksum;
    stored_info.created_at = std::chrono::system_clock::now();
    stored_info.last_accessed = stored_info.created_at;

    if (!write_metadata_file(cache_id, stored_info)) {
        // Metadata write failed, but data is saved
        std::cerr << "[FileCacheStore] Warning: metadata write failed for " << cache_id << std::endl;
    }

    // Update cache
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        metadata_cache_[cache_id] = stored_info;
        used_bytes_ += size;
    }

    // Sync to disk if requested
    if (options.sync_write) {
        // TODO: fsync
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.success = true;
    result.bytes_written = size;
    result.write_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.writes++;
        stats_.total_entries = metadata_cache_.size();
        stats_.total_size_bytes = used_bytes_;
    }

    return result;
}

CacheReadResult FileCacheStore::read(
    const std::string& cache_id,
    const CacheReadOptions& options
) {
    CacheReadResult result;
    auto start = std::chrono::high_resolution_clock::now();

    auto cache_path = get_cache_file_path(cache_id);

    if (!fs::exists(cache_path)) {
        result.success = false;
        result.error_message = "Cache entry not found";
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.misses++;
        return result;
    }

    // Get file size
    std::error_code ec;
    auto file_size = fs::file_size(cache_path, ec);
    if (ec) {
        result.success = false;
        result.error_message = "Failed to get file size";
        return result;
    }

    // Read file
    std::ifstream file(cache_path, std::ios::binary);
    if (!file) {
        result.success = false;
        result.error_message = "Failed to open file";
        return result;
    }

    result.data.resize(file_size);
    file.read(reinterpret_cast<char*>(result.data.data()), file_size);

    if (!file.good() && !file.eof()) {
        result.success = false;
        result.error_message = "Read failed";
        result.data.clear();
        return result;
    }

    // Check for compression header and decompress if needed
    if (options.decompress && Compressor::has_header(result.data.data(), result.data.size())) {
        Compressor compressor;
        auto decomp_result = compressor.decompress(result.data);

        if (!decomp_result.success) {
            result.success = false;
            result.error_message = "Decompression failed: " + decomp_result.error_message;
            result.data.clear();
            return result;
        }

        result.data = std::move(decomp_result.data);
        file_size = result.data.size();  // Update to decompressed size
    }

    // Verify checksum if requested
    if (options.verify_checksum) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = metadata_cache_.find(cache_id);
        if (it != metadata_cache_.end() && it->second.checksum != 0) {
            uint32_t computed = 0;
            // Compute CRC32 of read data
            const uint8_t* bytes = result.data.data();
            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < result.data.size(); ++i) {
                crc ^= bytes[i];
                for (int j = 0; j < 8; ++j) {
                    crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
                }
            }
            computed = ~crc;

            if (computed != it->second.checksum) {
                result.success = false;
                result.error_message = "Checksum mismatch";
                result.data.clear();
                return result;
            }
            result.checksum = computed;
        }
    }

    // Update access time
    touch(cache_id);

    auto end = std::chrono::high_resolution_clock::now();
    result.success = true;
    result.bytes_read = file_size;
    result.read_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.reads++;
        stats_.hits++;
    }

    return result;
}

CacheReadResult FileCacheStore::read_into(
    const std::string& cache_id,
    void* buffer,
    size_t buffer_size,
    const CacheReadOptions& options
) {
    CacheReadResult result;
    auto start = std::chrono::high_resolution_clock::now();

    auto cache_path = get_cache_file_path(cache_id);

    if (!fs::exists(cache_path)) {
        result.success = false;
        result.error_message = "Cache entry not found";
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.misses++;
        return result;
    }

    // Get file size
    std::error_code ec;
    auto file_size = fs::file_size(cache_path, ec);
    if (ec || file_size > buffer_size) {
        result.success = false;
        result.error_message = file_size > buffer_size ? "Buffer too small" : "Failed to get file size";
        return result;
    }

    // Read directly into buffer
    std::ifstream file(cache_path, std::ios::binary);
    if (!file) {
        result.success = false;
        result.error_message = "Failed to open file";
        return result;
    }

    file.read(static_cast<char*>(buffer), file_size);

    if (!file.good() && !file.eof()) {
        result.success = false;
        result.error_message = "Read failed";
        return result;
    }

    // Update access time
    touch(cache_id);

    auto end = std::chrono::high_resolution_clock::now();
    result.success = true;
    result.bytes_read = file_size;
    result.read_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.reads++;
        stats_.hits++;
    }

    return result;
}

bool FileCacheStore::remove(const std::string& cache_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = metadata_cache_.find(cache_id);
    if (it != metadata_cache_.end()) {
        used_bytes_ -= it->second.size_bytes;
        metadata_cache_.erase(it);
    }

    lock.unlock();

    // Delete files
    std::error_code ec;
    auto cache_path = get_cache_file_path(cache_id);
    auto meta_path = get_meta_file_path(cache_id);

    bool removed = false;
    if (fs::exists(cache_path)) {
        fs::remove(cache_path, ec);
        removed = !ec;
    }
    if (fs::exists(meta_path)) {
        fs::remove(meta_path, ec);
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.deletes++;
        stats_.total_entries = metadata_cache_.size();
        stats_.total_size_bytes = used_bytes_;
    }

    return removed;
}

bool FileCacheStore::exists(const std::string& cache_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (metadata_cache_.count(cache_id) > 0) {
        return true;
    }
    lock.unlock();

    // Check filesystem
    return fs::exists(get_cache_file_path(cache_id));
}

//=============================================================================
// Metadata Operations
//=============================================================================

std::optional<CacheEntryInfo> FileCacheStore::get_info(const std::string& cache_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = metadata_cache_.find(cache_id);
    if (it != metadata_cache_.end()) {
        return it->second;
    }

    lock.unlock();

    // Try loading from file
    return read_metadata_file(cache_id);
}

void FileCacheStore::touch(const std::string& cache_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = metadata_cache_.find(cache_id);
    if (it != metadata_cache_.end()) {
        it->second.last_accessed = std::chrono::system_clock::now();
        it->second.access_count++;
    }
}

std::vector<std::string> FileCacheStore::list() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::string> ids;
    ids.reserve(metadata_cache_.size());

    for (const auto& [id, _] : metadata_cache_) {
        ids.push_back(id);
    }

    return ids;
}

std::vector<std::string> FileCacheStore::list_by_prefix(const std::string& prefix) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::string> ids;

    for (const auto& [id, _] : metadata_cache_) {
        if (id.compare(0, prefix.size(), prefix) == 0) {
            ids.push_back(id);
        }
    }

    return ids;
}

std::vector<std::string> FileCacheStore::list_by_model(const std::string& model_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::string> ids;

    for (const auto& [id, info] : metadata_cache_) {
        if (info.model_id == model_id) {
            ids.push_back(id);
        }
    }

    return ids;
}

//=============================================================================
// Maintenance Operations
//=============================================================================

size_t FileCacheStore::compact() {
    // For file-based store, compact removes orphaned metadata files
    // and rebuilds the index
    size_t reclaimed = 0;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(store_path_, ec)) {
        if (entry.path().extension() == ".meta") {
            auto cache_id = entry.path().stem().string();
            auto cache_path = get_cache_file_path(cache_id);

            if (!fs::exists(cache_path)) {
                auto size = fs::file_size(entry.path(), ec);
                fs::remove(entry.path(), ec);
                if (!ec) {
                    reclaimed += size;
                }
            }
        }
    }

    rebuild_index();
    return reclaimed;
}

std::vector<std::string> FileCacheStore::verify_integrity() {
    std::vector<std::string> corrupted;

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto entries = metadata_cache_;
    lock.unlock();

    for (const auto& [id, info] : entries) {
        if (!verify(id)) {
            corrupted.push_back(id);
        }
    }

    return corrupted;
}

bool FileCacheStore::verify(const std::string& cache_id) {
    auto cache_path = get_cache_file_path(cache_id);

    if (!fs::exists(cache_path)) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = metadata_cache_.find(cache_id);
    if (it == metadata_cache_.end() || it->second.checksum == 0) {
        return true;  // No checksum to verify
    }

    uint32_t expected_checksum = it->second.checksum;
    lock.unlock();

    uint32_t actual_checksum = compute_file_checksum(cache_path);
    return actual_checksum == expected_checksum;
}

size_t FileCacheStore::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    size_t count = metadata_cache_.size();
    metadata_cache_.clear();
    used_bytes_ = 0;

    lock.unlock();

    // Delete all files
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(store_path_, ec)) {
        fs::remove(entry.path(), ec);
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_entries = 0;
        stats_.total_size_bytes = 0;
    }

    return count;
}

//=============================================================================
// Capacity Management
//=============================================================================

size_t FileCacheStore::capacity() const {
    return capacity_bytes_;
}

size_t FileCacheStore::used() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return used_bytes_;
}

void FileCacheStore::set_capacity(size_t bytes) {
    capacity_bytes_ = bytes;
    stats_.capacity_bytes = bytes;
}

//=============================================================================
// Statistics
//=============================================================================

CacheStoreStats FileCacheStore::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    CacheStoreStats stats = stats_;

    // Update current values
    std::shared_lock<std::shared_mutex> data_lock(mutex_);
    stats.total_entries = metadata_cache_.size();
    stats.total_size_bytes = used_bytes_;
    stats.capacity_bytes = capacity_bytes_;

    return stats;
}

void FileCacheStore::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.reads = 0;
    stats_.writes = 0;
    stats_.deletes = 0;
    stats_.hits = 0;
    stats_.misses = 0;
    stats_.avg_read_time_ms = 0.0;
    stats_.avg_write_time_ms = 0.0;
}

//=============================================================================
// Persistence
//=============================================================================

void FileCacheStore::sync() {
    // Save all metadata to disk
    std::shared_lock<std::shared_mutex> lock(mutex_);

    for (const auto& [id, info] : metadata_cache_) {
        write_metadata_file(id, info);
    }
}

std::string FileCacheStore::get_path() const {
    return store_path_.string();
}

//=============================================================================
// Additional Methods
//=============================================================================

void FileCacheStore::rebuild_index() {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    metadata_cache_.clear();
    used_bytes_ = 0;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(store_path_, ec)) {
        if (entry.path().extension() == ".kvc") {
            std::string cache_id = entry.path().stem().string();

            // Try to load metadata
            auto info = read_metadata_file(cache_id);
            if (info) {
                metadata_cache_[cache_id] = *info;
                used_bytes_ += info->size_bytes;
            } else {
                // Create basic metadata from file
                CacheEntryInfo basic_info;
                basic_info.cache_id = cache_id;
                basic_info.size_bytes = fs::file_size(entry.path(), ec);
                basic_info.created_at = std::chrono::system_clock::now();
                basic_info.last_accessed = basic_info.created_at;

                metadata_cache_[cache_id] = basic_info;
                used_bytes_ += basic_info.size_bytes;
            }
        }
    }
}

fs::path FileCacheStore::get_cache_file_path(const std::string& cache_id) const {
    return store_path_ / (cache_id + ".kvc");
}

fs::path FileCacheStore::get_meta_file_path(const std::string& cache_id) const {
    return store_path_ / (cache_id + ".meta");
}

//=============================================================================
// Private Helpers
//=============================================================================

bool FileCacheStore::write_metadata_file(const std::string& cache_id, const CacheEntryInfo& info) {
    auto meta_path = get_meta_file_path(cache_id);

    std::ofstream file(meta_path);
    if (!file) {
        return false;
    }

    // Write JSON metadata
    file << "{\n";
    file << "  \"cache_id\": \"" << info.cache_id << "\",\n";
    file << "  \"size_bytes\": " << info.size_bytes << ",\n";
    file << "  \"checksum\": " << info.checksum << ",\n";
    file << "  \"access_count\": " << info.access_count << ",\n";
    file << "  \"model_id\": \"" << info.model_id << "\",\n";
    file << "  \"num_layers\": " << info.num_layers << ",\n";
    file << "  \"num_heads\": " << info.num_heads << ",\n";
    file << "  \"head_dim\": " << info.head_dim << ",\n";
    file << "  \"sequence_length\": " << info.sequence_length << "\n";
    file << "}\n";

    return file.good();
}

std::optional<CacheEntryInfo> FileCacheStore::read_metadata_file(const std::string& cache_id) const {
    auto meta_path = get_meta_file_path(cache_id);

    if (!fs::exists(meta_path)) {
        return std::nullopt;
    }

    std::ifstream file(meta_path);
    if (!file) {
        return std::nullopt;
    }

    // Simple JSON parsing (for production, use nlohmann::json)
    CacheEntryInfo info;
    info.cache_id = cache_id;

    std::string line;
    while (std::getline(file, line)) {
        // Parse size_bytes
        if (line.find("\"size_bytes\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.size_bytes = std::stoull(line.substr(pos + 1));
            }
        }
        // Parse checksum
        if (line.find("\"checksum\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.checksum = std::stoul(line.substr(pos + 1));
            }
        }
        // Parse access_count
        if (line.find("\"access_count\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.access_count = std::stoull(line.substr(pos + 1));
            }
        }
        // Parse model_id
        if (line.find("\"model_id\"") != std::string::npos) {
            auto start = line.find(": \"");
            auto end = line.rfind('"');
            if (start != std::string::npos && end > start + 3) {
                info.model_id = line.substr(start + 3, end - start - 3);
            }
        }
        // Parse num_layers
        if (line.find("\"num_layers\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.num_layers = std::stoul(line.substr(pos + 1));
            }
        }
        // Parse num_heads
        if (line.find("\"num_heads\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.num_heads = std::stoul(line.substr(pos + 1));
            }
        }
        // Parse head_dim
        if (line.find("\"head_dim\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.head_dim = std::stoul(line.substr(pos + 1));
            }
        }
        // Parse sequence_length
        if (line.find("\"sequence_length\"") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                info.sequence_length = std::stoul(line.substr(pos + 1));
            }
        }
    }

    return info;
}

uint32_t FileCacheStore::compute_file_checksum(const fs::path& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return 0;
    }

    uint32_t crc = 0xFFFFFFFF;
    char buffer[4096];

    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        size_t count = static_cast<size_t>(file.gcount());
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffer);

        for (size_t i = 0; i < count; ++i) {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
            }
        }
    }

    return ~crc;
}

void FileCacheStore::update_used_bytes() const {
    std::error_code ec;
    size_t total = 0;

    for (const auto& entry : fs::directory_iterator(store_path_, ec)) {
        if (entry.path().extension() == ".kvc") {
            total += fs::file_size(entry.path(), ec);
        }
    }

    used_bytes_ = total;
}

} // namespace snapllm
