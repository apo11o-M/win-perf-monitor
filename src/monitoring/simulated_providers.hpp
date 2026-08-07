#pragma once

#include "component_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <random>

namespace perfmon::monitoring {

class SimulatedCpuProvider final : public ComponentProvider {
public:
    explicit SimulatedCpuProvider(std::size_t logical_processor_count = 24);
    void Sample(model::SystemSample& sample) override;

private:
    std::size_t logical_processor_count_ = 24;
    std::uint64_t tick_ = 0;
    std::mt19937 generator_{0xC0FFEEU};
    std::uniform_real_distribution<float> noise_{-4.0F, 4.0F};
};

class SimulatedGpuProvider final : public ComponentProvider {
public:
    void Sample(model::SystemSample& sample) override;

private:
    std::uint64_t tick_ = 0;
    std::mt19937 generator_{0x5070U};
    std::uniform_real_distribution<float> noise_{-3.0F, 3.0F};
};

} // namespace perfmon::monitoring
