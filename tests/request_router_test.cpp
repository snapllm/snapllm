#include "snapllm/request_router.h"
#include <cassert>
#include <iostream>

using namespace snapllm;

int main() {
    const std::vector<std::string> models{"chat-7b", "vision-vl"};
    const std::vector<ModelType> types{ModelType::TEXT_LLM, ModelType::MULTIMODAL_VL};

    auto text = RequestRouter::choose({"", "", "text"}, models, types, "chat-7b");
    assert(text.accepted && text.model == "chat-7b");

    auto vision = RequestRouter::choose({"", "", "vision"}, models, types, "chat-7b");
    assert(vision.accepted && vision.model == "vision-vl");

    auto wrong_route = RequestRouter::choose({"chat-7b", "", "vision"}, models, types, "chat-7b");
    assert(!wrong_route.accepted && wrong_route.error.find("does not support") != std::string::npos);

    auto missing = RequestRouter::choose({"missing", "", "text"}, models, types, "chat-7b");
    assert(!missing.accepted && missing.error.find("not loaded") != std::string::npos);

    auto task = RequestRouter::choose({"", "vision", "vision"}, models, types, "chat-7b");
    assert(task.accepted && task.model == "vision-vl");

    std::cout << "request_router_test: PASS\n";
    return 0;
}
