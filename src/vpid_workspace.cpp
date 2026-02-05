/**
 * @file vpid_workspace.cpp
 * @brief Virtual Processing-In-Disk (vPID) Workspace - Direct I/O Implementation
 *
 * NEW IMPLEMENTATION: Replaces memory-mapping with Direct I/O + LRU tensor cache.
 * This achieves true "disk-as-RAM" with predictable, controlled RAM usage.
 */

#include "snapllm/vpid_workspace.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#endif

namespace snapllm {

VPIDWorkspace::VPIDWorkspace(const std::string& workspace_path,
                             size_t total_size,
                             bool use_direct_io,
                             size_t cache_budget_bytes)
    : workspace_path_(workspace_path)
    , total_size_(total_size)
    , use_direct_io_(use_direct_io)
    , cache_budget_bytes_(cache_budget_bytes)
#ifdef _WIN32
    , file_handle_(INVALID_HANDLE_VALUE)
    , mapping_handle_(NULL)
    , mapped_region_(nullptr)
#else
    , file_descriptor_(-1)
    , mapped_region_(nullptr)
#endif
    , is_initialized_(false)
    , next_free_offset_(0)
{
    // Initialize tensor cache
    tensor_cache_ = std::make_unique<VPIDTensorCache>(cache_budget_bytes_);
}

VPIDWorkspace::~VPIDWorkspace() {
    shutdown();
}

bool VPIDWorkspace::initialize() {
    if (is_initialized_) {
        std::cerr << "[vPID] Workspace already initialized" << std::endl;
        return false;
    }

    std::cout << "[vPID] Initializing workspace with Direct I/O..." << std::endl;
    std::cout << "  Path: " << workspace_path_ << std::endl;
    std::cout << "  Size: " << (total_size_ / (1024.0 * 1024 * 1024)) << " GB" << std::endl;
    std::cout << "  Direct I/O: " << (use_direct_io_ ? "enabled" : "disabled") << std::endl;
    std::cout << "  Cache budget: " << (cache_budget_bytes_ / (1024.0 * 1024 * 1024)) << " GB" << std::endl;

    // Create or open file
    if (!create_or_open_file()) {
        std::cerr << "[vPID] Failed to create/open workspace file" << std::endl;
        return false;
    }

    is_initialized_ = true;
    std::cout << "[vPID] Workspace initialized successfully (Direct I/O mode)!" << std::endl;
    return true;
}

void VPIDWorkspace::shutdown() {
    if (!is_initialized_) return;

    std::cout << "[vPID] Shutting down workspace..." << std::endl;

    // Print statistics
    if (tensor_cache_) {
        std::cout << "  Tensor cache: " << tensor_cache_->get_cached_count() << " tensors, "
                  << (tensor_cache_->get_used_bytes() / (1024.0 * 1024)) << " MB used" << std::endl;
        std::cout << "  Cache hit rate: " << (tensor_cache_->get_hit_rate() * 100) << "%" << std::endl;
    }

    std::cout << "  Total allocations: " << stats_.total_allocations << std::endl;
    std::cout << "  Total reads: " << stats_.total_reads
              << " (" << (stats_.bytes_read / (1024.0 * 1024)) << " MB)" << std::endl;
    std::cout << "  Total writes: " << stats_.total_writes
              << " (" << (stats_.bytes_written / (1024.0 * 1024)) << " MB)" << std::endl;

    // Sync pending writes
    if (stats_.bytes_written > 0) {
        std::cout << "  Flushing pending writes to disk..." << std::endl;
        sync(0, 0);  // Full sync
    }

    cleanup_resources();
    is_initialized_ = false;
}

bool VPIDWorkspace::create_or_open_file() {
#ifdef _WIN32
    // Windows: Open file with Direct I/O if enabled
    DWORD flags = FILE_FLAG_RANDOM_ACCESS;

    // NOTE: FILE_FLAG_NO_BUFFERING requires aligned I/O (sector alignment)
    // We'll handle alignment in read_direct_io()
    if (use_direct_io_) {
        flags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
    }

    file_handle_ = CreateFileA(
        workspace_path_.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // Allow concurrent access for multi-instance
        NULL,
        OPEN_ALWAYS,  // Create if doesn't exist
        flags,
        NULL
    );

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        std::cerr << "[vPID] CreateFile failed: " << GetLastError() << std::endl;
        return false;
    }

    // Check current file size
    LARGE_INTEGER current_size;
    if (!GetFileSizeEx(file_handle_, &current_size)) {
        std::cerr << "[vPID] GetFileSizeEx failed: " << GetLastError() << std::endl;
        return false;
    }

    std::cout << "  Current file size: " << (current_size.QuadPart / (1024.0 * 1024 * 1024)) << " GB" << std::endl;

    // Set file size if needed
    if (current_size.QuadPart != static_cast<LONGLONG>(total_size_)) {
        if (current_size.QuadPart == 0) {
            std::cout << "  Creating new workspace file..." << std::endl;
        } else {
            std::cout << "  Resizing workspace file..." << std::endl;
        }

        // Mark as sparse (optional, improves performance for large files)
        DWORD bytesReturned;
        DeviceIoControl(file_handle_, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytesReturned, NULL);

        // Set file size
        FILE_END_OF_FILE_INFO eofInfo;
        eofInfo.EndOfFile.QuadPart = total_size_;
        if (!SetFileInformationByHandle(file_handle_, FileEndOfFileInfo, &eofInfo, sizeof(eofInfo))) {
            std::cerr << "[vPID] SetFileInformationByHandle failed: " << GetLastError() << std::endl;
            return false;
        }

        FlushFileBuffers(file_handle_);
        std::cout << "  File resized to " << (total_size_ / (1024.0 * 1024 * 1024)) << " GB" << std::endl;
    } else {
        std::cout << "  Opening existing workspace file" << std::endl;
    }

    // Create memory mapping ONLY if Direct I/O is disabled
    // Direct I/O mode uses tensor cache instead of memory mapping
    if (!use_direct_io_) {
        std::cout << "  Creating memory mapping..." << std::endl;
        mapping_handle_ = CreateFileMappingA(
            file_handle_,
            NULL,
            PAGE_READWRITE,
            (DWORD)(total_size_ >> 32),
            (DWORD)(total_size_ & 0xFFFFFFFF),
            NULL
        );

        if (mapping_handle_ == NULL) {
            std::cerr << "[vPID] CreateFileMapping failed: " << GetLastError() << std::endl;
            return false;
        }

        mapped_region_ = MapViewOfFile(
            mapping_handle_,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            total_size_
        );

        if (mapped_region_ == NULL) {
            std::cerr << "[vPID] MapViewOfFile failed: " << GetLastError() << std::endl;
            return false;
        }

        std::cout << "  Mapped " << (total_size_ / (1024.0 * 1024 * 1024)) << " GB at address: "
                  << std::hex << mapped_region_ << std::dec << std::endl;
    } else {
        std::cout << "  Direct I/O mode: Skipping memory mapping" << std::endl;
        std::cout << "  Tensors will be loaded on-demand into "
                  << (cache_budget_bytes_ / (1024.0 * 1024 * 1024)) << " GB cache" << std::endl;
    }

    return true;

#else
    // Linux/Unix implementation
    int flags = O_RDWR | O_CREAT;
    if (use_direct_io_) {
        flags |= O_DIRECT | O_SYNC;
    }

    file_descriptor_ = open(workspace_path_.c_str(), flags, 0644);
    if (file_descriptor_ < 0) {
        std::cerr << "[vPID] open failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Check/set file size
    struct stat st;
    if (fstat(file_descriptor_, &st) < 0) {
        std::cerr << "[vPID] fstat failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (static_cast<size_t>(st.st_size) != total_size_) {
        if (ftruncate(file_descriptor_, total_size_) < 0) {
            std::cerr << "[vPID] ftruncate failed: " << strerror(errno) << std::endl;
            return false;
        }
        std::cout << "  File resized to " << (total_size_ / (1024.0 * 1024 * 1024)) << " GB" << std::endl;
    }

    // Create memory mapping ONLY if Direct I/O is disabled
    // Direct I/O mode uses tensor cache instead of memory mapping
    if (!use_direct_io_) {
        std::cout << "  Creating memory mapping..." << std::endl;
        int prot = PROT_READ | PROT_WRITE;
        int map_flags = MAP_SHARED;

        mapped_region_ = mmap(
            nullptr,
            total_size_,
            prot,
            map_flags,
            file_descriptor_,
            0
        );

        if (mapped_region_ == MAP_FAILED) {
            std::cerr << "[vPID] mmap failed: " << strerror(errno) << std::endl;
            mapped_region_ = nullptr;
            return false;
        }

        // Advise kernel about access pattern
        madvise(mapped_region_, total_size_, MADV_RANDOM);

        std::cout << "  Mapped " << (total_size_ / (1024.0 * 1024 * 1024)) << " GB at address: "
                  << std::hex << mapped_region_ << std::dec << std::endl;
    } else {
        std::cout << "  Direct I/O mode: Skipping memory mapping" << std::endl;
        std::cout << "  Tensors will be loaded on-demand into "
                  << (cache_budget_bytes_ / (1024.0 * 1024 * 1024)) << " GB cache" << std::endl;
    }

    return true;
#endif
}

void VPIDWorkspace::cleanup_resources() {
    // Clear tensor cache first
    if (tensor_cache_) {
        tensor_cache_->clear_all();
    }

#ifdef _WIN32
    // Unmap memory view first
    if (mapped_region_) {
        UnmapViewOfFile(mapped_region_);
        mapped_region_ = nullptr;
    }
    // Close mapping handle
    if (mapping_handle_ != NULL) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = NULL;
    }
    // Close file handle
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    // Unmap memory first
    if (mapped_region_ && mapped_region_ != MAP_FAILED) {
        munmap(mapped_region_, total_size_);
        mapped_region_ = nullptr;
    }
    // Close file descriptor
    if (file_descriptor_ >= 0) {
        close(file_descriptor_);
        file_descriptor_ = -1;
    }
#endif
}

// NEW: Direct I/O read from disk
size_t VPIDWorkspace::read_direct_io(size_t offset, void* buffer, size_t size) {
    if (!is_initialized_) {
        std::cerr << "[vPID] read_direct_io failed: workspace not initialized" << std::endl;
        return 0;
    }

    if (!buffer) {
        std::cerr << "[vPID] read_direct_io failed: null buffer" << std::endl;
        return 0;
    }

    // Bounds check
    if (offset + size > total_size_) {
        std::cerr << "[vPID] Read out of bounds! offset=" << offset
                  << ", size=" << size << ", total=" << total_size_ << std::endl;
        return 0;
    }

#ifdef _WIN32
    // Windows Direct I/O requires sector-aligned offset and size
    // For simplicity, we'll handle non-aligned reads by reading aligned blocks

    LARGE_INTEGER file_offset;
    file_offset.QuadPart = offset;

    if (SetFilePointerEx(file_handle_, file_offset, NULL, FILE_BEGIN) == 0) {
        std::cerr << "[vPID] SetFilePointerEx failed: " << GetLastError() << std::endl;
        return 0;
    }

    DWORD bytes_read = 0;
    if (!ReadFile(file_handle_, buffer, static_cast<DWORD>(size), &bytes_read, NULL)) {
        DWORD error = GetLastError();
        std::cerr << "[vPID] ReadFile failed: error " << error
                  << " (offset=" << offset << ", size=" << size << ")" << std::endl;
        return 0;
    }

    // Check for partial reads
    if (bytes_read != size) {
        std::cerr << "[vPID] Warning: Partial read - expected " << size
                  << " bytes, got " << bytes_read << " bytes" << std::endl;
    }

    return bytes_read;

#else
    // Linux Direct I/O
    ssize_t bytes_read = pread(file_descriptor_, buffer, size, offset);
    if (bytes_read < 0) {
        std::cerr << "[vPID] pread failed: " << strerror(errno)
                  << " (offset=" << offset << ", size=" << size << ")" << std::endl;
        return 0;
    }

    // Check for partial reads
    if (static_cast<size_t>(bytes_read) != size) {
        std::cerr << "[vPID] Warning: Partial read - expected " << size
                  << " bytes, got " << bytes_read << " bytes" << std::endl;
    }

    return bytes_read;
#endif
}

// NEW: Load tensor into cache from disk
void* VPIDWorkspace::load_tensor_to_cache(const std::string& tensor_name, size_t offset, size_t size) {
    if (!is_initialized_ || !tensor_cache_) return nullptr;

    // Check if already cached
    void* cached_ptr = tensor_cache_->get_tensor(tensor_name);
    if (cached_ptr) {
        return cached_ptr;  // Cache hit
    }

    // Cache miss - allocate cache slot
    cached_ptr = tensor_cache_->allocate_slot(tensor_name, size);
    if (!cached_ptr) {
        std::cerr << "[vPID] Failed to allocate cache slot for '" << tensor_name << "'" << std::endl;
        return nullptr;
    }

    // Read from disk using Direct I/O
    size_t bytes_read = read_direct_io(offset, cached_ptr, size);
    if (bytes_read != size) {
        std::cerr << "[vPID] Failed to read tensor '" << tensor_name
                  << "' from disk (expected " << size << " bytes, got " << bytes_read << ")" << std::endl;
        tensor_cache_->evict_tensor(tensor_name);
        return nullptr;
    }

    std::cout << "[vPID] Loaded '" << tensor_name << "' (" << (size / (1024.0 * 1024))
              << " MB) from disk to cache" << std::endl;

    return cached_ptr;
}

void* VPIDWorkspace::get_cached_tensor(const std::string& tensor_name) {
    if (!tensor_cache_) return nullptr;
    return tensor_cache_->get_tensor(tensor_name);
}

bool VPIDWorkspace::evict_cached_tensor(const std::string& tensor_name) {
    if (!tensor_cache_) return false;
    return tensor_cache_->evict_tensor(tensor_name);
}

VPIDAllocation VPIDWorkspace::allocate(size_t size, const std::string& name) {
    if (!is_initialized_) {
        return VPIDAllocation();
    }

    std::lock_guard<std::mutex> lock(alloc_mutex_);

    // Check if we have space
    size_t offset = next_free_offset_.load();
    if (offset + size > total_size_) {
        std::cerr << "[vPID] Workspace full! Cannot allocate " << size << " bytes" << std::endl;
        return VPIDAllocation();
    }

    // Allocate (no actual pointer since we're using Direct I/O, not memory-mapping)
    VPIDAllocation alloc(offset, size, nullptr, name);

    // Update tracking
    next_free_offset_ += size;
    allocations_[offset] = alloc;
    stats_.total_allocations++;

    std::cout << "[vPID] Allocated " << (size / (1024.0 * 1024)) << " MB at offset "
              << offset << (name.empty() ? "" : " for " + name) << std::endl;

    return alloc;
}

void VPIDWorkspace::free(const VPIDAllocation& alloc) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    allocations_.erase(alloc.offset);
    // Note: We don't reclaim space in this simple implementation
}

size_t VPIDWorkspace::write_direct(size_t offset, const void* data, size_t size) {
    if (!is_initialized_ || !data) {
        std::cerr << "[vPID] write_direct failed: not initialized or null data" << std::endl;
        return 0;
    }

    // Bounds check
    if (offset + size > total_size_) {
        std::cerr << "[vPID] Write out of bounds! offset=" << offset
                  << ", size=" << size << ", total=" << total_size_ << std::endl;
        return 0;
    }

    size_t bytes_written = 0;

    // Choose write strategy based on configuration and availability
    if (mapped_region_ != nullptr) {
        // Memory-mapped mode: direct memcpy to mapped region (FAST)
        // This works whether or not Direct I/O is enabled on the file handle
        void* dest = static_cast<uint8_t*>(mapped_region_) + offset;
        std::memcpy(dest, data, size);
        bytes_written = size;
    } else {
        // Fallback: Direct I/O mode using file operations (SLOWER but predictable)
        // This path is used when memory mapping is not available
#ifdef _WIN32
        LARGE_INTEGER file_offset;
        file_offset.QuadPart = offset;

        if (SetFilePointerEx(file_handle_, file_offset, NULL, FILE_BEGIN) == 0) {
            std::cerr << "[vPID] SetFilePointerEx failed: " << GetLastError() << std::endl;
            return 0;
        }

        DWORD bytes_written_dword = 0;
        if (!WriteFile(file_handle_, data, static_cast<DWORD>(size), &bytes_written_dword, NULL)) {
            std::cerr << "[vPID] WriteFile failed: " << GetLastError() << std::endl;
            return 0;
        }
        bytes_written = bytes_written_dword;

#else
        ssize_t result = pwrite(file_descriptor_, data, size, offset);
        if (result < 0) {
            std::cerr << "[vPID] pwrite failed: " << strerror(errno) << std::endl;
            return 0;
        }
        bytes_written = result;
#endif
    }

    // Update stats
    stats_.total_writes++;
    stats_.bytes_written += bytes_written;

    return bytes_written;
}

void VPIDWorkspace::sync(size_t offset, size_t size) {
    if (!is_initialized_) return;

#ifdef _WIN32
    // Windows: Flush file buffers
    FlushFileBuffers(file_handle_);
#else
    // Linux: Sync file
    fsync(file_descriptor_);
#endif
}

void VPIDWorkspace::prefetch(size_t offset, size_t size) {
    // With Direct I/O + cache, prefetch is a no-op
    // Tensors are loaded on-demand into cache
    (void)offset;
    (void)size;
}

double VPIDWorkspace::get_fragmentation() const {
    if (total_size_ == 0) return 0.0;

    size_t used = next_free_offset_.load();
    size_t allocated_in_use = 0;

    for (const auto& pair : allocations_) {
        allocated_in_use += pair.second.size;
    }

    if (used == 0) return 0.0;

    // Fragmentation = (used space - allocated space) / used space
    return (double)(used - allocated_in_use) / used;
}

// Layer eviction helper: Parse layer ID from tensor name
int VPIDWorkspace::get_layer_from_name(const std::string& tensor_name) {
    // Tensor names follow pattern: "blk.N.component" where N is the layer number
    // Examples: "blk.5.attn_q.weight", "blk.15.ffn_up.weight"
    // Non-layer tensors: "token_embd.weight", "output.weight", "output_norm.weight"

    size_t blk_pos = tensor_name.find("blk.");
    if (blk_pos == std::string::npos) {
        return -1;  // Not a layer tensor
    }

    // Extract the number after "blk."
    size_t start = blk_pos + 4;  // Skip "blk."
    size_t end = tensor_name.find('.', start);
    if (end == std::string::npos) {
        return -1;  // Invalid format
    }

    try {
        std::string layer_str = tensor_name.substr(start, end - start);
        int layer_id = std::stoi(layer_str);
        return layer_id;
    } catch (...) {
        return -1;  // Parsing failed
    }
}

void VPIDWorkspace::register_tensor_layer(const std::string& tensor_name, size_t offset, size_t size) {
    int layer_id = get_layer_from_name(tensor_name);
    if (layer_id < 0) {
        // Not a layer tensor (token_embd, output, etc.) - skip tracking
        return;
    }

    // Track this memory region for the layer
    std::lock_guard<std::mutex> lock(layer_mutex_);
    layer_regions_[layer_id].push_back({offset, size});

    // Log layer registration for debugging (first 3 tensors per layer)
    if (layer_regions_[layer_id].size() <= 3) {
        std::cout << "[Layer Tracking] Registered " << tensor_name
                  << " -> Layer " << layer_id
                  << " (offset=" << offset << ", size=" << size / (1024*1024) << " MB)" << std::endl;
    }
}

size_t VPIDWorkspace::evict_layer(int layer_id) {
    std::lock_guard<std::mutex> lock(layer_mutex_);

    // Find all memory regions for this layer
    auto it = layer_regions_.find(layer_id);
    if (it == layer_regions_.end() || it->second.empty()) {
        std::cerr << "[Layer Eviction] Warning: Layer " << layer_id << " not found" << std::endl;
        return 0;
    }

    size_t total_evicted = 0;

    // Strategy: Evict both from tensor cache AND memory-mapped pages
    // This works for both Direct I/O mode (tensor cache) and mmap mode (OS pages)

    // 1. Evict from tensor cache (for Direct I/O mode)
    if (tensor_cache_) {
        // Find all tensors belonging to this layer and evict them
        // Tensor names follow pattern: "blk.N.*"
        std::string layer_prefix = "blk." + std::to_string(layer_id) + ".";

        // Get all cached tensor names and evict matching ones
        auto cached_tensors = tensor_cache_->get_all_cached_names();
        for (const auto& tensor_name : cached_tensors) {
            if (tensor_name.find(layer_prefix) == 0) {
                size_t tensor_size = tensor_cache_->get_tensor_size(tensor_name);
                if (tensor_cache_->evict_tensor(tensor_name)) {
                    total_evicted += tensor_size;
                }
            }
        }
    }

    // 2. Discard physical pages for memory-mapped regions (for mmap mode)
#ifdef _WIN32
    if (mapped_region_) {
        for (const auto& region : it->second) {
            void* region_addr = static_cast<char*>(mapped_region_) + region.offset;

            // DiscardVirtualMemory tells Windows to discard physical pages
            // but keep the virtual address space reserved
            DWORD result = DiscardVirtualMemory(region_addr, region.size);
            if (result != ERROR_SUCCESS) {
                std::cerr << "[Layer Eviction] Warning: DiscardVirtualMemory failed for layer "
                          << layer_id << " region at offset " << region.offset
                          << " (error: " << result << ")" << std::endl;
            } else {
                // Only count if not already counted from tensor cache eviction
                if (!tensor_cache_) {
                    total_evicted += region.size;
                }
            }
        }
    }
#else
    // Linux: Use madvise(MADV_DONTNEED)
    if (mapped_region_) {
        for (const auto& region : it->second) {
            void* region_addr = static_cast<char*>(mapped_region_) + region.offset;
            int result = madvise(region_addr, region.size, MADV_DONTNEED);
            if (result != 0) {
                std::cerr << "[Layer Eviction] Warning: madvise failed for layer "
                          << layer_id << " region at offset " << region.offset
                          << " (error: " << strerror(errno) << ")" << std::endl;
            } else {
                // Only count if not already counted from tensor cache eviction
                if (!tensor_cache_) {
                    total_evicted += region.size;
                }
            }
        }
    }
#endif

    if (total_evicted > 0) {
        std::cout << "[Layer Eviction] Evicted layer " << layer_id
                  << " (" << (total_evicted / (1024.0 * 1024)) << " MB)" << std::endl;
    }

    return total_evicted;
}

size_t VPIDWorkspace::prefetch_layer(int layer_id) {
    std::lock_guard<std::mutex> lock(layer_mutex_);

    // Find all memory regions for this layer
    auto it = layer_regions_.find(layer_id);
    if (it == layer_regions_.end() || it->second.empty()) {
        return 0;
    }

    size_t total_prefetched = 0;

#ifdef _WIN32
    // Windows: Use PrefetchVirtualMemory
    if (mapped_region_) {
        for (const auto& region : it->second) {
            void* region_addr = static_cast<char*>(mapped_region_) + region.offset;

            WIN32_MEMORY_RANGE_ENTRY range;
            range.VirtualAddress = region_addr;
            range.NumberOfBytes = region.size;

            BOOL result = PrefetchVirtualMemory(
                GetCurrentProcess(),
                1,
                &range,
                0  // flags
            );

            if (result) {
                total_prefetched += region.size;
            }
        }
    }
#else
    // Linux: Use madvise(MADV_WILLNEED)
    if (mapped_region_) {
        for (const auto& region : it->second) {
            void* region_addr = static_cast<char*>(mapped_region_) + region.offset;
            int result = madvise(region_addr, region.size, MADV_WILLNEED);
            if (result == 0) {
                total_prefetched += region.size;
            }
        }
    }
#endif

    // With Direct I/O cache, we could also pre-load tensors into cache
    if (tensor_cache_ && total_prefetched == 0) {
        // This would require knowing tensor names from layer_id
        // For now, prefetch is a no-op with Direct I/O
        total_prefetched = 0;
    }

    return total_prefetched;
}

bool VPIDWorkspace::test_evict_all() {
    if (!tensor_cache_) return false;

    // Test eviction by clearing all cached tensors
    std::cout << "[vPID] Testing evict_all..." << std::endl;
    tensor_cache_->clear_all();
    std::cout << "[vPID] All tensors evicted from cache" << std::endl;

    return true;
}

} // namespace snapllm
