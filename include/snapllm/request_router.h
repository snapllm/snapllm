#pragma once

#include "model_types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace snapllm {

struct RouteRequest {
    std::string requested_model;
    std::string task;
    std::string modality = "text";
};

struct RouteDecision {
    bool accepted = false;
    std::string model;
    std::string error;
    ModelType type = ModelType::UNKNOWN;
};

/** Explicit, point-in-time scheduling signals for one resident model.
 *  Callers own synchronization and should pass a consistent snapshot. */
struct ModelLoad {
    std::size_t in_flight = 0;
    double average_latency_ms = 0.0;
};

/** Deterministic, explainable routing for the local API. */
class RequestRouter {
public:
    static RouteDecision choose(const RouteRequest& request,
                                const std::vector<std::string>& loaded_models,
                                const std::vector<ModelType>& model_types,
                                const std::string& current_model);

    /**
     * Load-aware automatic routing. Explicit model requests remain pinned and
     * are never redirected. For automatic requests, compatible candidates are
     * ranked by in-flight count, then average latency; exact ties use the
     * supplied round-robin cursor to avoid starving a model. The cursor is an
     * explicit input so this function is thread-safe and deterministic for a
     * given snapshot/cursor.
     */
    static RouteDecision choose(const RouteRequest& request,
                                const std::vector<std::string>& loaded_models,
                                const std::vector<ModelType>& model_types,
                                const std::string& current_model,
                                const std::vector<ModelLoad>& loads,
                                std::uint64_t round_robin_cursor = 0);
};

} // namespace snapllm
