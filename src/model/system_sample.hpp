#pragma once

#include "metric_value.hpp"

#include <chrono>
#include <vector>

namespace perfmon::model {

using SampleClock = std::chrono::steady_clock;
using SampleTime = SampleClock::time_point;

struct CpuSample {
    MetricValue total_utilization{};

    // Kept as a dynamic series so Phase 4 can expose one value per logical
    // processor without redesigning the sampling/history pipeline.
    std::vector<MetricValue> logical_processor_utilization{};
};

struct GpuSample {
    MetricValue total_utilization{};
    MetricValue memory_utilization{};
};

struct SystemSample {
    SampleTime timestamp{};
    CpuSample cpu{};
    GpuSample gpu{};
};

} // namespace perfmon::model
