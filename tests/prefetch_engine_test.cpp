#include "snapllm/prefetch_engine.h"

#include <cassert>

int main() {
    snapllm::PrefetchEngine engine(nullptr);
    engine.record_pattern({"layer.0", "layer.1", "layer.2", "layer.1"});
    const auto prediction = engine.predict_next("layer.1");
    assert(prediction.size() == 1);
    assert(prediction.front() == "layer.2");

    // Without tensor metadata, prefetch cannot load data and must report misses.
    engine.prefetch({"layer.0", "layer.1"});
    assert(engine.get_hit_rate() == 0.0);
    engine.reset_stats();
    assert(engine.get_hit_rate() == 0.0);
    return 0;
}
