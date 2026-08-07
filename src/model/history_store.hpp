#pragma once

#include "history_buffer.hpp"
#include "system_sample.hpp"

#include <chrono>
#include <cstddef>
#include <vector>

namespace perfmon::model {

struct HistorySettings {
    std::chrono::milliseconds sampling_interval{1000};
    std::chrono::seconds visible_duration{60};
    std::chrono::seconds maximum_duration{120};
    std::chrono::milliseconds minimum_sampling_interval{1000};

    [[nodiscard]] std::size_t RequiredCapacity() const noexcept;
};

struct TimestampedMetric {
    SampleTime timestamp{};
    MetricValue metric{};
};

struct MetricSeriesSnapshot {
    std::vector<TimestampedMetric> samples{};
    MetricValue latest{};
};

struct PerformanceSnapshot {
    SampleTime window_start{};
    SampleTime window_end{};
    std::chrono::seconds visible_duration{60};

    MetricSeriesSnapshot cpu_total{};
    std::vector<MetricSeriesSnapshot> cpu_logical_processors{};
    CpuInfo cpu_info{};
    MetricSeriesSnapshot gpu_total{};
    MetricSeriesSnapshot gpu_memory{};
    GpuInfo gpu_info{};
};

class MetricHistory {
public:
    explicit MetricHistory(std::size_t capacity);

    void Push(SampleTime timestamp, MetricValue metric);
    [[nodiscard]] MetricSeriesSnapshot SnapshotSince(SampleTime start) const;
    [[nodiscard]] SampleTime LatestTimestamp() const noexcept;

private:
    HistoryBuffer<TimestampedMetric> samples_;
};

class HistoryStore {
public:
    explicit HistoryStore(HistorySettings settings = {});

    void Push(const SystemSample& sample);
    void SetVisibleDuration(std::chrono::seconds duration) noexcept;

    [[nodiscard]] const HistorySettings& Settings() const noexcept;
    [[nodiscard]] PerformanceSnapshot Snapshot() const;

private:
    void EnsureLogicalProcessorHistories(std::size_t count);

    HistorySettings settings_{};
    MetricHistory cpu_total_;
    std::vector<MetricHistory> cpu_logical_processors_{};
    MetricHistory gpu_total_;
    MetricHistory gpu_memory_;
    CpuInfo latest_cpu_info_{};
    GpuInfo latest_gpu_info_{};
    SampleTime latest_timestamp_{};
    bool has_sample_ = false;
};

} // namespace perfmon::model
