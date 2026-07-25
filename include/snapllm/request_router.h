#pragma once

#include "model_types.h"
#include <string>
#include <vector>

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

/** Deterministic, explainable routing for the local API. */
class RequestRouter {
public:
    static RouteDecision choose(const RouteRequest& request,
                                const std::vector<std::string>& loaded_models,
                                const std::vector<ModelType>& model_types,
                                const std::string& current_model);
};

} // namespace snapllm
