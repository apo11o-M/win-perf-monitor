#include "simulated_providers.hpp"

#include <algorithm>
#include <cmath>

namespace perfmon::monitoring {
namespace {

constexpr float kPi = 3.14159265358979323846F;

float Wave(std::uint64_t tick, float period, float phase = 0.0F) {
    const float angle = (static_cast<float>(tick) / period) * (2.0F * kPi) + phase;
    return std::sin(angle);
}

} // namespace

void SimulatedGpuProvider::Sample(model::SystemSample& sample) {
    const float workload = std::max(0.0F, Wave(tick_, 29.0F));
    const float secondary = Wave(tick_, 11.0F, 0.8F);
    const float gpu = std::clamp(
        9.0F + (workload * 61.0F) + (secondary * 9.0F) + noise_(generator_),
        0.0F,
        98.0F);

    const float memory = std::clamp(
        37.0F + (Wave(tick_, 53.0F, 0.4F) * 9.0F) + (workload * 11.0F),
        20.0F,
        78.0F);

    sample.gpu.total_utilization = model::MetricValue::ValidPercentage(gpu);
    sample.gpu.memory_utilization = model::MetricValue::ValidPercentage(memory);
    ++tick_;
}

} // namespace perfmon::monitoring
