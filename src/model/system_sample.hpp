#pragma once

#include "metric_value.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace perfmon::model {

using SampleClock = std::chrono::steady_clock;
using SampleTime = SampleClock::time_point;

struct CpuInfo {
    std::wstring processor_name{};
    std::optional<std::uint32_t> physical_core_count{};
    std::optional<std::uint32_t> logical_processor_count{};

    // Dynamic system-wide CPU summary values sampled once per interval.
    // Physical/logical counts remain available for topology and graph layout,
    // but the UI prioritizes these Task Manager-style runtime statistics.
    MetricValue current_speed_ghz{};
    std::optional<std::uint32_t> process_count{};
    std::optional<std::uint32_t> thread_count{};
    std::optional<std::chrono::milliseconds> system_uptime{};
};

struct CpuSample {
    MetricValue total_utilization{};

    // One entry per active Windows logical processor. The ordering is stable
    // for the lifetime of the provider and follows processor-group order.
    std::vector<MetricValue> logical_processor_utilization{};

    // Keep static identity/topology and the latest runtime summary together in
    // the provider-owned sample so rendering never performs Windows queries.
    CpuInfo info{};
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
