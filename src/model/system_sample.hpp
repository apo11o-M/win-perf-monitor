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

struct GpuInfo {
    std::wstring gpu_name{};
    std::wstring driver_version{};
    std::wstring provider_status{};

    MetricValue memory_used_gib{};
    MetricValue memory_total_gib{};
    MetricValue temperature_c{};
    MetricValue power_w{};
    MetricValue graphics_clock_mhz{};
    MetricValue memory_clock_mhz{};
};

struct GpuSample {
    MetricValue total_utilization{};

    // Percentage of dedicated framebuffer memory currently allocated. This is
    // intentionally distinct from NVML's memory-activity utilization counter.
    MetricValue memory_utilization{};
    GpuInfo info{};
};

struct SystemSample {
    SampleTime timestamp{};
    CpuSample cpu{};
    GpuSample gpu{};
};

} // namespace perfmon::model
