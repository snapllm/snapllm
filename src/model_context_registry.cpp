/**
 * @file model_context_registry.cpp
 * @brief Implementation of Model-Context Auto-Association Registry
 */

#include "snapllm/model_context_registry.h"
#include "snapllm/context_manager.h"
#include "snapllm/file_cache_store.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace snapllm {

//=============================================================================
// Construction
//=============================================================================

ModelContextRegistry::ModelContextRegistry(
    const WorkspacePaths& paths,
    ContextManager* context_manager
)
    : paths_(paths)
    , context_manager_(context_manager)
{
    // Try to load existing index
    if (!load_index()) {
        // No index found, rebuild from disk
        rebuild_index();
    }
}

ModelContextRegistry::~ModelContextRegistry() {
    // Save index on shutdown
    save_index();
}

//=============================================================================
// Discovery Operations
//=============================================================================

ModelContextDiscovery ModelContextRegistry::discover_contexts(
    const std::string& model_id,
    bool force_scan
) {
    auto start = std::chrono::high_resolution_clock::now();

    ModelContextDiscovery result;
    result.model_id = model_id;

    std::vector<ContextIndexEntry> entries;

    if (force_scan) {
        // Full disk scan
        entries = scan_disk_for_model(model_id);

        // Update index
        {
            std::unique_lock lock(mutex_);
            model_contexts_[model_id] = entries;
            for (const auto& entry : entries) {
                context_to_model_[entry.context_id] = model_id;
            }
        }

        stats_.index_misses++;
        result.from_cache = false;
    } else {
        // Try cached index first
        {
            std::shared_lock lock(mutex_);
            auto it = model_contexts_.find(model_id);
            if (it != model_contexts_.end()) {
                entries = it->second;
                stats_.index_hits++;
                result.from_cache = true;
            }
        }

        if (entries.empty() && !result.from_cache) {
            // Not in index, do disk scan
            entries = scan_disk_for_model(model_id);

            // Update index
            {
                std::unique_lock lock(mutex_);
                model_contexts_[model_id] = entries;
                for (const auto& entry : entries) {
                    context_to_model_[entry.context_id] = model_id;
                }
            }

            stats_.index_misses++;
            result.from_cache = false;
        }
    }

    // Convert entries to discovered contexts
    for (const auto& entry : entries) {
        auto discovered = entry_to_discovered(entry);

        // Check if loaded in context manager
        if (context_manager_) {
            ContextHandle handle;
            handle.id = entry.context_id;
            handle.valid = true;
            discovered.is_loaded = context_manager_->is_loaded(handle);
        }

        result.contexts.push_back(std::move(discovered));
        result.total_tokens += entry.token_count;
        result.total_storage_bytes += entry.storage_size_bytes;
        if (discovered.is_loaded) {
            result.loaded_contexts++;
        }
    }

    result.total_contexts = result.contexts.size();

    auto end = std::chrono::high_resolution_clock::now();
    result.scan_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // Notify callback
    if (discovery_callback_) {
        discovery_callback_(result);
    }

    return result;
}

std::vector<std::string> ModelContextRegistry::get_context_ids(const std::string& model_id) const {
    std::shared_lock lock(mutex_);

    std::vector<std::string> ids;
    auto it = model_contexts_.find(model_id);
    if (it != model_contexts_.end()) {
        ids.reserve(it->second.size());
        for (const auto& entry : it->second) {
            ids.push_back(entry.context_id);
        }
    }

    return ids;
}

std::vector<std::string> ModelContextRegistry::get_models_with_contexts() const {
    std::shared_lock lock(mutex_);

    std::vector<std::string> models;
    models.reserve(model_contexts_.size());
    for (const auto& [model_id, _] : model_contexts_) {
        models.push_back(model_id);
    }

    return models;
}

bool ModelContextRegistry::has_contexts(const std::string& model_id) const {
    std::shared_lock lock(mutex_);
    auto it = model_contexts_.find(model_id);
    return it != model_contexts_.end() && !it->second.empty();
}

size_t ModelContextRegistry::context_count(const std::string& model_id) const {
    std::shared_lock lock(mutex_);
    auto it = model_contexts_.find(model_id);
    return it != model_contexts_.end() ? it->second.size() : 0;
}

//=============================================================================
// Index Management
//=============================================================================

size_t ModelContextRegistry::rebuild_index() {
    auto start = std::chrono::high_resolution_clock::now();

    // Scan all contexts from disk
    auto all_entries = scan_all_contexts();

    // Rebuild index
    {
        std::unique_lock lock(mutex_);
        model_contexts_.clear();
        context_to_model_.clear();

        for (auto& entry : all_entries) {
            model_contexts_[entry.model_id].push_back(entry);
            context_to_model_[entry.context_id] = entry.model_id;
        }

        stats_.total_models = model_contexts_.size();
        stats_.total_contexts = all_entries.size();
        stats_.total_storage_bytes = 0;
        for (const auto& entry : all_entries) {
            stats_.total_storage_bytes += entry.storage_size_bytes;
        }
        stats_.last_rebuild = std::chrono::system_clock::now();
    }

    // Save updated index
    save_index();

    return all_entries.size();
}

void ModelContextRegistry::register_context(const ContextIndexEntry& entry) {
    std::unique_lock lock(mutex_);

    // Add to model's context list
    model_contexts_[entry.model_id].push_back(entry);
    context_to_model_[entry.context_id] = entry.model_id;

    // Update stats
    stats_.total_contexts++;
    stats_.total_storage_bytes += entry.storage_size_bytes;
    if (model_contexts_.size() > stats_.total_models) {
        stats_.total_models = model_contexts_.size();
    }
}

void ModelContextRegistry::unregister_context(const std::string& context_id) {
    std::unique_lock lock(mutex_);

    // Find model for this context
    auto model_it = context_to_model_.find(context_id);
    if (model_it == context_to_model_.end()) {
        return;
    }

    std::string model_id = model_it->second;
    context_to_model_.erase(model_it);

    // Remove from model's context list
    auto& contexts = model_contexts_[model_id];
    auto it = std::find_if(contexts.begin(), contexts.end(),
        [&](const ContextIndexEntry& e) { return e.context_id == context_id; });

    if (it != contexts.end()) {
        stats_.total_storage_bytes -= it->storage_size_bytes;
        contexts.erase(it);
        stats_.total_contexts--;
    }

    // Clean up empty model entries
    if (contexts.empty()) {
        model_contexts_.erase(model_id);
        stats_.total_models--;
    }
}

void ModelContextRegistry::touch_context(const std::string& context_id) {
    std::unique_lock lock(mutex_);

    auto model_it = context_to_model_.find(context_id);
    if (model_it == context_to_model_.end()) {
        return;
    }

    auto& contexts = model_contexts_[model_it->second];
    auto it = std::find_if(contexts.begin(), contexts.end(),
        [&](const ContextIndexEntry& e) { return e.context_id == context_id; });

    if (it != contexts.end()) {
        it->last_accessed = std::chrono::system_clock::now();
    }
}

bool ModelContextRegistry::save_index() {
    try {
        std::shared_lock lock(mutex_);

        json index;
        index["version"] = 1;
        index["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        json models_array = json::array();
        for (const auto& [model_id, contexts] : model_contexts_) {
            json model_entry;
            model_entry["model_id"] = model_id;

            json contexts_array = json::array();
            for (const auto& ctx : contexts) {
                json ctx_entry;
                ctx_entry["context_id"] = ctx.context_id;
                ctx_entry["name"] = ctx.name;
                ctx_entry["file_path"] = ctx.file_path;
                ctx_entry["token_count"] = ctx.token_count;
                ctx_entry["storage_size_bytes"] = ctx.storage_size_bytes;
                ctx_entry["content_hash"] = ctx.content_hash;
                ctx_entry["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                    ctx.created_at.time_since_epoch()).count();
                ctx_entry["last_accessed"] = std::chrono::duration_cast<std::chrono::seconds>(
                    ctx.last_accessed.time_since_epoch()).count();
                contexts_array.push_back(ctx_entry);
            }

            model_entry["contexts"] = contexts_array;
            models_array.push_back(model_entry);
        }

        index["models"] = models_array;

        // Write to file
        std::string index_path = get_index_path();
        fs::create_directories(fs::path(index_path).parent_path());

        std::ofstream file(index_path);
        if (!file) {
            return false;
        }

        file << index.dump(2);
        stats_.last_save = std::chrono::system_clock::now();

        return true;

    } catch (const std::exception&) {
        return false;
    }
}

bool ModelContextRegistry::load_index() {
    try {
        std::string index_path = get_index_path();
        if (!fs::exists(index_path)) {
            return false;
        }

        std::ifstream file(index_path);
        if (!file) {
            return false;
        }

        json index = json::parse(file);

        std::unique_lock lock(mutex_);
        model_contexts_.clear();
        context_to_model_.clear();

        for (const auto& model_entry : index["models"]) {
            std::string model_id = model_entry["model_id"];

            for (const auto& ctx_entry : model_entry["contexts"]) {
                ContextIndexEntry entry;
                entry.context_id = ctx_entry["context_id"];
                entry.model_id = model_id;
                entry.name = ctx_entry.value("name", "");
                entry.file_path = ctx_entry.value("file_path", "");
                entry.token_count = ctx_entry.value("token_count", 0);
                entry.storage_size_bytes = ctx_entry.value("storage_size_bytes", 0);
                entry.content_hash = ctx_entry.value("content_hash", "");

                if (ctx_entry.contains("created_at")) {
                    entry.created_at = std::chrono::system_clock::time_point(
                        std::chrono::seconds(ctx_entry["created_at"].get<int64_t>()));
                }
                if (ctx_entry.contains("last_accessed")) {
                    entry.last_accessed = std::chrono::system_clock::time_point(
                        std::chrono::seconds(ctx_entry["last_accessed"].get<int64_t>()));
                }

                model_contexts_[model_id].push_back(entry);
                context_to_model_[entry.context_id] = model_id;
            }
        }

        // Update stats
        stats_.total_models = model_contexts_.size();
        stats_.total_contexts = context_to_model_.size();
        stats_.total_storage_bytes = 0;
        for (const auto& [_, contexts] : model_contexts_) {
            for (const auto& ctx : contexts) {
                stats_.total_storage_bytes += ctx.storage_size_bytes;
            }
        }

        return true;

    } catch (const std::exception&) {
        return false;
    }
}

//=============================================================================
// Validation
//=============================================================================

size_t ModelContextRegistry::validate_index() {
    std::vector<std::string> invalid_contexts;

    {
        std::shared_lock lock(mutex_);
        for (const auto& [model_id, contexts] : model_contexts_) {
            for (const auto& ctx : contexts) {
                if (!validate_entry(ctx)) {
                    invalid_contexts.push_back(ctx.context_id);
                }
            }
        }
    }

    // Remove invalid entries
    for (const auto& ctx_id : invalid_contexts) {
        unregister_context(ctx_id);
    }

    if (!invalid_contexts.empty()) {
        save_index();
    }

    return invalid_contexts.size();
}

bool ModelContextRegistry::is_context_valid(const std::string& context_id) const {
    std::shared_lock lock(mutex_);

    auto model_it = context_to_model_.find(context_id);
    if (model_it == context_to_model_.end()) {
        return false;
    }

    auto& contexts = model_contexts_.at(model_it->second);
    auto it = std::find_if(contexts.begin(), contexts.end(),
        [&](const ContextIndexEntry& e) { return e.context_id == context_id; });

    return it != contexts.end() && validate_entry(*it);
}

//=============================================================================
// Statistics
//=============================================================================

ModelContextRegistry::Stats ModelContextRegistry::get_stats() const {
    std::shared_lock lock(mutex_);
    return stats_;
}

void ModelContextRegistry::set_discovery_callback(DiscoveryCallback callback) {
    discovery_callback_ = std::move(callback);
}

//=============================================================================
// Internal Helpers
//=============================================================================

std::string ModelContextRegistry::get_index_path() const {
    return (paths_.contexts_cold / "context_index.json").string();
}

std::vector<ContextIndexEntry> ModelContextRegistry::scan_disk_for_model(
    const std::string& model_id
) {
    std::vector<ContextIndexEntry> entries;

    // Scan all contexts and filter by model_id
    auto all_entries = scan_all_contexts();
    for (auto& entry : all_entries) {
        if (entry.model_id == model_id) {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

std::vector<ContextIndexEntry> ModelContextRegistry::scan_all_contexts() {
    std::vector<ContextIndexEntry> entries;

    fs::path kv_dir = paths_.contexts_cold;
    if (!fs::exists(kv_dir)) {
        return entries;
    }

    // Scan for context metadata files
    // Expected structure:
    //   kv_cache_dir/
    //     ctx_001/
    //       metadata.json   <- Contains model_id, name, etc.
    //       kv_data.bin     <- KV cache data
    //     ctx_002/
    //       ...

    for (const auto& dir_entry : fs::directory_iterator(kv_dir)) {
        if (!dir_entry.is_directory()) {
            continue;
        }

        std::string context_id = dir_entry.path().filename().string();
        fs::path metadata_path = dir_entry.path() / "metadata.json";
        fs::path kv_data_path = dir_entry.path() / "kv_data.bin";

        if (!fs::exists(metadata_path)) {
            continue;
        }

        try {
            std::ifstream meta_file(metadata_path);
            if (!meta_file) {
                continue;
            }

            json metadata = json::parse(meta_file);

            ContextIndexEntry entry;
            entry.context_id = context_id;
            entry.model_id = metadata.value("model_id", "");
            entry.name = metadata.value("name", "");
            entry.file_path = kv_data_path.string();
            entry.token_count = metadata.value("token_count", 0);
            entry.content_hash = metadata.value("content_hash", "");

            // Get file size
            if (fs::exists(kv_data_path)) {
                entry.storage_size_bytes = fs::file_size(kv_data_path);
                entry.file_size = entry.storage_size_bytes;

                auto ftime = fs::last_write_time(kv_data_path);
                entry.file_mtime = std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count();
            }

            // Parse timestamps
            if (metadata.contains("created_at")) {
                entry.created_at = std::chrono::system_clock::time_point(
                    std::chrono::seconds(metadata["created_at"].get<int64_t>()));
            }
            if (metadata.contains("last_accessed")) {
                entry.last_accessed = std::chrono::system_clock::time_point(
                    std::chrono::seconds(metadata["last_accessed"].get<int64_t>()));
            }

            // Skip entries without model_id
            if (!entry.model_id.empty()) {
                entries.push_back(std::move(entry));
            }

        } catch (const std::exception&) {
            // Skip malformed entries
            continue;
        }
    }

    return entries;
}

DiscoveredContext ModelContextRegistry::entry_to_discovered(
    const ContextIndexEntry& entry
) const {
    DiscoveredContext discovered;

    discovered.context_id = entry.context_id;
    discovered.model_id = entry.model_id;
    discovered.name = entry.name;
    discovered.token_count = entry.token_count;
    discovered.storage_size_bytes = entry.storage_size_bytes;
    discovered.created_at = entry.created_at;
    discovered.last_accessed = entry.last_accessed;

    // Determine tier based on whether loaded
    discovered.tier = "cold";  // Default: on disk

    // Check validity
    discovered.is_valid = validate_entry(entry);
    if (!discovered.is_valid) {
        discovered.error_message = "Context file not found or corrupted";
    }

    return discovered;
}

bool ModelContextRegistry::validate_entry(const ContextIndexEntry& entry) const {
    // Check if KV data file exists
    if (!entry.file_path.empty() && !fs::exists(entry.file_path)) {
        return false;
    }

    // Check file size matches (if recorded)
    if (entry.file_size > 0 && fs::exists(entry.file_path)) {
        if (fs::file_size(entry.file_path) != entry.file_size) {
            return false;  // File was modified
        }
    }

    return true;
}

} // namespace snapllm
