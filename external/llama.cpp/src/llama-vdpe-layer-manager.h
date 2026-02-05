#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <memory>
#include <chrono>
#include <mutex>
#include <functional>

// Forward declarations
struct ggml_tensor;
struct llama_tensor_weight;
namespace snapllm { class VPIDWorkspace; }

namespace vdpe {

// Layer state lifecycle
enum class LayerState {
    COLD,       // Not loaded in RAM (disk-only)
    WARMING,    // Currently being loaded into RAM
    HOT,        // Fully loaded in RAM and ready
    EVICTING    // Currently being evicted from RAM
};

// Tensor metadata for layer management
struct TensorInfo {
    std::string name;
    int layer_num;          // -1 for non-layer tensors (embeddings, output)
    size_t size_bytes;
    std::string f32_file_path;
    LayerState state;
    float* mapped_ptr;      // Memory-mapped pointer
    uint64_t last_access_time;  // For LRU eviction

    TensorInfo() : layer_num(-1), size_bytes(0), state(LayerState::COLD),
                   mapped_ptr(nullptr), last_access_time(0) {}
};

// Layer metadata
struct LayerInfo {
    int layer_num;
    LayerState state;
    std::vector<std::string> tensor_names;  // All tensors in this layer
    size_t total_size_bytes;
    uint64_t last_access_time;

    LayerInfo() : layer_num(-1), state(LayerState::COLD),
                  total_size_bytes(0), last_access_time(0) {}
};

// Configuration for layer management
struct VDPEConfig {
    int max_initial_layers = 4;      // Initial layers to wire on startup
    size_t max_ram_budget_gb = 4;    // Maximum RAM to use for cached layers
    bool enable_eviction = true;     // Enable LRU eviction when memory pressure occurs
    bool enable_prefetch = true;     // Prefetch next layer before it's needed
    int prefetch_lookahead = 2;      // How many layers ahead to prefetch
};

/**
 * VDPELayerManager: Modular layer-aware progressive loading and eviction manager
 *
 * Responsibilities:
 * - Track layer states (COLD/WARMING/HOT/EVICTING)
 * - Progressive loading: Only load first N layers initially
 * - Dynamic loading: Load layers on-demand during inference
 * - LRU eviction: Evict least-recently-used layers when RAM budget exceeded
 * - Prefetching: Load next layers before they're needed
 */
class VDPELayerManager {
public:
    VDPELayerManager(snapllm::VPIDWorkspace* workspace,
                     const std::string& cache_dir,
                     const VDPEConfig& config = VDPEConfig());

    ~VDPELayerManager();

    // === Initialization ===

    /**
     * Build layer state by scanning cache directory for .f32 files
     * Returns total number of layers found
     */
    int build_layer_state_from_cache();

    /**
     * Register tensor metadata for tracking
     */
    void register_tensor(const std::string& name,
                        llama_tensor_weight* tensor,
                        size_t size_bytes);

    // === Progressive Loading ===

    /**
     * Wire initial layers (first max_initial_layers)
     * Returns number of tensors wired
     */
    size_t wire_initial_layers();

    /**
     * Wire a specific layer on-demand
     * Returns true if successful
     */
    bool wire_layer(int layer_num);

    /**
     * Wire a specific tensor on-demand
     * Returns true if successful
     */
    bool wire_tensor(const std::string& tensor_name);

    // === Layer Eviction ===

    /**
     * Check if RAM budget is exceeded
     */
    bool is_memory_pressure() const;

    /**
     * Evict least-recently-used layer to free RAM
     * Returns true if eviction successful
     */
    bool evict_lru_layer();

    /**
     * Evict specific layer
     */
    bool evict_layer(int layer_num);

    // === Inference Callbacks ===

    /**
     * Callback before layer computation
     * Triggers on-demand loading if layer is COLD
     */
    void on_before_layer_compute(int layer_num);

    /**
     * Callback after layer computation
     * Updates access time for LRU tracking
     */
    void on_after_layer_compute(int layer_num);

    // === State Queries ===

    LayerState get_layer_state(int layer_num) const;
    LayerState get_tensor_state(const std::string& tensor_name) const;

    size_t get_total_wired_bytes() const { return total_wired_bytes_; }
    size_t get_ram_budget_bytes() const { return config_.max_ram_budget_gb * 1024ULL * 1024ULL * 1024ULL; }

    int get_num_layers() const { return num_layers_; }
    int get_num_wired_layers() const;
    int get_num_deferred_layers() const;

    // === Statistics ===

    struct Stats {
        size_t total_tensors = 0;
        size_t wired_tensors = 0;
        size_t deferred_tensors = 0;
        size_t total_bytes = 0;
        size_t wired_bytes = 0;
        size_t deferred_bytes = 0;
        int num_layers = 0;
        int num_wired_layers = 0;
        int num_evictions = 0;
        int num_dynamic_loads = 0;
    };

    Stats get_stats() const;
    void print_stats() const;

private:
    // === Helper Methods ===

    /**
     * Extract layer number from tensor name
     * Returns -1 for non-layer tensors
     */
    int extract_layer_num(const std::string& tensor_name) const;

    /**
     * Memory-map individual .f32 file
     */
    float* mmap_tensor_file(const std::string& f32_path, size_t expected_elements);

    /**
     * Unmap tensor from memory
     */
    void unmap_tensor(const std::string& tensor_name);

    /**
     * Update access time for LRU tracking
     */
    void touch_layer(int layer_num);
    void touch_tensor(const std::string& tensor_name);

    /**
     * Find least-recently-used layer
     */
    int find_lru_layer() const;

    // === Member Variables ===

    snapllm::VPIDWorkspace* workspace_;
    std::string cache_dir_;
    VDPEConfig config_;

    int num_layers_;
    size_t total_wired_bytes_;

    // Tensor tracking
    std::unordered_map<std::string, TensorInfo> tensors_;
    std::unordered_map<std::string, llama_tensor_weight*> tensor_weights_;

    // Layer tracking
    std::unordered_map<int, LayerInfo> layers_;

    // Statistics
    mutable Stats stats_;

    // Thread safety
    mutable std::mutex mutex_;
};

} // namespace vdpe
