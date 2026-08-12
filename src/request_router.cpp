#include "snapllm/request_router.h"
#include <algorithm>
#include <cctype>
#include <limits>

namespace snapllm {
namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
ModelType modality_type(const std::string& modality) {
    const auto value = lower(modality);
    if (value == "text" || value == "chat") return ModelType::TEXT_LLM;
    if (value == "vision" || value == "multimodal") return ModelType::MULTIMODAL_VL;
    if (value == "image") return ModelType::IMAGE_DIFFUSION;
    if (value == "video") return ModelType::VIDEO_DIFFUSION;
    return ModelType::UNKNOWN;
}

RouteDecision choose_load_aware(const RouteRequest& request,
                                const std::vector<std::string>& loaded_models,
                                const std::vector<ModelType>& model_types,
                                const std::vector<ModelLoad>& loads,
                                std::uint64_t cursor) {
    RouteDecision decision;
    const ModelType required = modality_type(request.modality);
    if (required == ModelType::UNKNOWN) {
        decision.error = "Unsupported routing modality: " + request.modality;
        return decision;
    }
    if (loaded_models.size() != model_types.size() || loaded_models.size() != loads.size()) {
        decision.error = "Routing registry is inconsistent";
        return decision;
    }
    auto compatible = [&](size_t index) {
        return required == ModelType::TEXT_LLM
            ? model_types[index] == ModelType::TEXT_LLM
            : model_types[index] == required;
    };

    // Explicit model requests are pinned: load balancing must never silently
    // redirect a caller that selected a particular resident model.
    if (!request.requested_model.empty()) {
        auto it = std::find(loaded_models.begin(), loaded_models.end(), request.requested_model);
        if (it == loaded_models.end()) {
            decision.error = "Requested model is not loaded: " + request.requested_model;
            return decision;
        }
        const size_t index = static_cast<size_t>(std::distance(loaded_models.begin(), it));
        if (!compatible(index)) {
            decision.error = "Requested model does not support modality '" + request.modality + "'";
            return decision;
        }
        decision.model = *it;
        decision.type = model_types[index];
        decision.accepted = true;
        return decision;
    }

    std::vector<size_t> candidates;
    candidates.reserve(loaded_models.size());
    const auto wanted = lower(request.task);
    for (size_t i = 0; i < loaded_models.size(); ++i) {
        if (compatible(i) && (wanted.empty() || lower(loaded_models[i]).find(wanted) != std::string::npos)) {
            candidates.push_back(i);
        }
    }
    // A task hint is a preference. If no resident model name matches it,
    // fall back to all compatible models rather than rejecting valid work.
    if (candidates.empty() && !wanted.empty()) {
        for (size_t i = 0; i < loaded_models.size(); ++i) {
            if (compatible(i)) candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        decision.error = "No loaded model supports modality '" + request.modality + "'";
        return decision;
    }

    std::size_t min_in_flight = std::numeric_limits<std::size_t>::max();
    double min_latency = std::numeric_limits<double>::infinity();
    for (const auto index : candidates) {
        min_in_flight = std::min(min_in_flight, loads[index].in_flight);
    }
    std::vector<size_t> least_busy;
    for (const auto index : candidates) {
        if (loads[index].in_flight == min_in_flight) {
            min_latency = std::min(min_latency, loads[index].average_latency_ms);
            least_busy.push_back(index);
        }
    }
    std::vector<size_t> tied;
    for (const auto index : least_busy) {
        if (loads[index].average_latency_ms == min_latency) tied.push_back(index);
    }
    const size_t selected = tied[static_cast<size_t>(cursor % tied.size())];
    decision.model = loaded_models[selected];
    decision.type = model_types[selected];
    decision.accepted = true;
    return decision;
}
}

RouteDecision RequestRouter::choose(const RouteRequest& request,
                                    const std::vector<std::string>& loaded_models,
                                    const std::vector<ModelType>& model_types,
                                    const std::string& current_model) {
    RouteDecision decision;
    const ModelType required = modality_type(request.modality);
    if (required == ModelType::UNKNOWN) {
        decision.error = "Unsupported routing modality: " + request.modality;
        return decision;
    }
    if (loaded_models.size() != model_types.size()) {
        decision.error = "Routing registry is inconsistent";
        return decision;
    }

    auto compatible = [&](size_t index) {
        return required == ModelType::TEXT_LLM
            ? model_types[index] == ModelType::TEXT_LLM
            : model_types[index] == required;
    };

    if (!request.requested_model.empty()) {
        auto it = std::find(loaded_models.begin(), loaded_models.end(), request.requested_model);
        if (it == loaded_models.end()) {
            decision.error = "Requested model is not loaded: " + request.requested_model;
            return decision;
        }
        const size_t index = static_cast<size_t>(std::distance(loaded_models.begin(), it));
        if (!compatible(index)) {
            decision.error = "Requested model does not support modality '" + request.modality + "'";
            return decision;
        }
        decision.model = *it;
        decision.type = model_types[index];
        decision.accepted = true;
        return decision;
    }

    if (!request.task.empty()) {
        const auto wanted = lower(request.task);
        for (size_t i = 0; i < loaded_models.size(); ++i) {
            if (compatible(i) && lower(loaded_models[i]).find(wanted) != std::string::npos) {
                decision.model = loaded_models[i];
                decision.type = model_types[i];
                decision.accepted = true;
                return decision;
            }
        }
    }

    if (!current_model.empty()) {
        auto it = std::find(loaded_models.begin(), loaded_models.end(), current_model);
        if (it != loaded_models.end()) {
            const size_t index = static_cast<size_t>(std::distance(loaded_models.begin(), it));
            if (compatible(index)) {
                decision.model = *it;
                decision.type = model_types[index];
                decision.accepted = true;
                return decision;
            }
        }
    }

    // The active model is only a preference. If it cannot satisfy the
    // requested modality, route to the first compatible resident model rather
    // than failing a valid request merely because a text model is active.
    for (size_t i = 0; i < loaded_models.size(); ++i) {
        if (compatible(i)) {
            decision.model = loaded_models[i];
            decision.type = model_types[i];
            decision.accepted = true;
            return decision;
        }
    }
    decision.error = "No loaded model supports modality '" + request.modality + "'";
    return decision;
}

RouteDecision RequestRouter::choose(const RouteRequest& request,
                                    const std::vector<std::string>& loaded_models,
                                    const std::vector<ModelType>& model_types,
                                    const std::string& current_model,
                                    const std::vector<ModelLoad>& loads,
                                    std::uint64_t round_robin_cursor) {
    (void)current_model;
    return choose_load_aware(request, loaded_models, model_types, loads, round_robin_cursor);
}
} // namespace snapllm
