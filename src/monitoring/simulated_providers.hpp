#pragma once

#include "component_provider.hpp"

#include <cstdint>
#include <random>

namespace perfmon::monitoring {

// Retained only until Phase 5 replaces it with live NVML telemetry.
class SimulatedGpuProvider final : public ComponentProvider {
public:
    void Sample(model::SystemSample& sample) override;

private:
    std::uint64_t tick_ = 0;
    std::mt19937 generator_{0x5070U};
    std::uniform_real_distribution<float> noise_{-3.0F, 3.0F};
};

} // namespace perfmon::monitoring
