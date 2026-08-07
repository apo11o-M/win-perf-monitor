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

SimulatedCpuProvider::SimulatedCpuProvider(std::size_t logical_processor_count)
    : logical_processor_count_(logical_processor_count) {}

void SimulatedCpuProvider::Sample(model::SystemSample& sample) {
    sample.cpu.logical_processor_utilization.resize(logical_processor_count_);

    float total = 0.0F;
    for (std::size_t index = 0; index < logical_processor_count_; ++index) {
        const float phase = static_cast<float>(index) * 0.37F;
        const float burst = std::max(0.0F, Wave(tick_, 17.0F, phase));
        const float slow = Wave(tick_, 43.0F, phase * 0.35F);
        const float utilization = std::clamp(
            19.0F + (slow * 10.0F) + (burst * 33.0F) + noise_(generator_),
            1.0F,
            96.0F);

        sample.cpu.logical_processor_utilization[index] =
            model::MetricValue::ValidPercentage(utilization);
        total += utilization;
    }

    const float average = logical_processor_count_ == 0
        ? 0.0F
        : total / static_cast<float>(logical_processor_count_);
    sample.cpu.total_utilization = model::MetricValue::ValidPercentage(average);
    ++tick_;
}

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
