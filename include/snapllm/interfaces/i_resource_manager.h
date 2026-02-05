/**
 * @file i_resource_manager.h
 * @brief Interface for vPID Resource Managers
 *
 * Base interface that defines the contract for all vPID resource managers.
 * Both ModelManager (L1) and ContextManager (L2) implement this interface.
 *
 * Design Principles:
 * - Uniform resource lifecycle management
 * - Async operations for non-blocking behavior
 * - Type-safe handles
 * - Observable statistics
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <future>
#include <functional>
#include <cstdint>
#include <chrono>

namespace snapllm {

/**
 * @brief Resource handle - type-safe wrapper for resource identifiers
 *
 * Provides compile-time distinction between different handle types
 * (e.g., ModelHandle vs ContextHandle)
 *
 * NOTE: Public members for backwards compatibility with existing code.
 * Methods also provided for newer code.
 */
template<typename Tag>
struct ResourceHandle {
    // Public members (for backwards compatibility)
    std::string id;
    bool valid = false;
    std::chrono::system_clock::time_point created_at;

    // Constructors
    ResourceHandle() : id(""), valid(false) {}
    explicit ResourceHandle(const std::string& id_) : id(id_), valid(!id_.empty()) {}

    // Method accessors (for newer code)
    const std::string& get_id() const { return id; }
    bool is_valid() const { return valid; }
    explicit operator bool() const { return valid; }

    bool operator==(const ResourceHandle& other) const { return id == other.id; }
    bool operator!=(const ResourceHandle& other) const { return id != other.id; }
    bool operator<(const ResourceHandle& other) const { return id < other.id; }
};

// Type tags for handle distinction
struct ModelHandleTag {};
struct ContextHandleTag {};

// Concrete handle types
using ModelHandle = ResourceHandle<ModelHandleTag>;
using ContextHandle = ResourceHandle<ContextHandleTag>;

/**
 * @brief Resource status enumeration
 */
enum class ResourceStatus {
    Unknown,        ///< Status not determined
    Loading,        ///< Currently being loaded
    Ready,          ///< Loaded and ready for use
    Unloading,      ///< Being unloaded
    Evicted,        ///< Evicted from hot storage, still in cold storage
    Error           ///< Error state
};

/**
 * @brief Resource statistics
 */
struct ResourceStats {
    uint64_t access_count = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    size_t memory_bytes = 0;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;
};

/**
 * @brief Resource metadata base structure
 */
struct ResourceMetadata {
    std::string id;
    std::string name;
    ResourceStatus status = ResourceStatus::Unknown;
    ResourceStats stats;

    virtual ~ResourceMetadata() = default;
};

/**
 * @brief Interface for resource managers (L1: Models, L2: Contexts)
 *
 * @tparam SpecT Specification type for loading resources
 * @tparam HandleT Handle type for referencing loaded resources
 * @tparam MetadataT Metadata type for resource information
 *
 * Contract:
 * - load_async() returns unique handle for each successful load
 * - is_loaded(h) == true iff get_metadata(h).has_value()
 * - unload(h) returns true only if is_loaded(h) was true
 * - list() returns exactly the handles for which is_loaded() is true
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - Concurrent load_async() calls may execute in parallel
 * - load_async() and unload() for same handle are serialized
 */
template<typename SpecT, typename HandleT, typename MetadataT>
class IResourceManager {
public:
    virtual ~IResourceManager() = default;

    //=========================================================================
    // Lifecycle Operations
    //=========================================================================

    /**
     * @brief Load resource asynchronously
     * @param spec Resource specification
     * @return Future that resolves to resource handle
     * @throws std::runtime_error on load failure
     */
    virtual std::future<HandleT> load_async(const SpecT& spec) = 0;

    /**
     * @brief Load resource synchronously (blocking)
     * @param spec Resource specification
     * @return Resource handle, or invalid handle on failure
     */
    virtual HandleT load_sync(const SpecT& spec) {
        return load_async(spec).get();
    }

    /**
     * @brief Unload resource
     * @param handle Resource handle
     * @return true if resource was unloaded, false if not found
     */
    virtual bool unload(const HandleT& handle) = 0;

    /**
     * @brief Check if resource is loaded
     * @param handle Resource handle
     * @return true if resource is loaded and ready
     */
    virtual bool is_loaded(const HandleT& handle) const = 0;

    //=========================================================================
    // Query Operations
    //=========================================================================

    /**
     * @brief Get resource metadata
     * @param handle Resource handle
     * @return Metadata if resource exists, nullopt otherwise
     */
    virtual std::optional<MetadataT> get_metadata(const HandleT& handle) const = 0;

    /**
     * @brief Get resource status
     * @param handle Resource handle
     * @return Resource status
     */
    virtual ResourceStatus get_status(const HandleT& handle) const = 0;

    /**
     * @brief List all loaded resource handles
     * @return Vector of handles
     */
    virtual std::vector<HandleT> list() const = 0;

    //=========================================================================
    // Statistics
    //=========================================================================

    /**
     * @brief Get total memory usage
     * @return Memory usage in bytes
     */
    virtual size_t memory_usage() const = 0;

    /**
     * @brief Get resource count
     * @return Number of loaded resources
     */
    virtual size_t count() const = 0;

    /**
     * @brief Get aggregate statistics
     * @return Statistics structure
     */
    virtual ResourceStats get_stats() const = 0;
};

/**
 * @brief Callback type for resource events
 */
template<typename HandleT>
using ResourceCallback = std::function<void(const HandleT&, ResourceStatus)>;

/**
 * @brief Extended interface with event notifications
 */
template<typename SpecT, typename HandleT, typename MetadataT>
class IObservableResourceManager : public IResourceManager<SpecT, HandleT, MetadataT> {
public:
    /**
     * @brief Register callback for resource events
     * @param callback Function to call on events
     * @return Subscription ID for unregistering
     */
    virtual uint64_t on_status_change(ResourceCallback<HandleT> callback) = 0;

    /**
     * @brief Unregister callback
     * @param subscription_id ID returned from on_status_change
     */
    virtual void remove_callback(uint64_t subscription_id) = 0;
};

} // namespace snapllm

// Hash specialization for handles (for use in unordered containers)
namespace std {
    template<typename Tag>
    struct hash<snapllm::ResourceHandle<Tag>> {
        size_t operator()(const snapllm::ResourceHandle<Tag>& h) const {
            return std::hash<std::string>{}(h.id());
        }
    };
}
