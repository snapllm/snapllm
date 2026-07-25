#include "snapllm/request_router.h"
#include <algorithm>
#include <cctype>

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
    decision.error = "No loaded model supports modality '" + request.modality + "'";
    return decision;
}
} // namespace snapllm
