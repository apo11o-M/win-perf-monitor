#include "history_store.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace perfmon::model {

std::size_t HistorySettings::RequiredCapacity() const noexcept {
    const auto max_ms = std::chrono::duration_cast<std::chrono::milliseconds>(maximum_duration).count();
    const auto min_ms = std::max<std::int64_t>(1, minimum_sampling_interval.count());
    const auto sample_count = static_cast<std::size_t>((max_ms + min_ms - 1) / min_ms);

    // One extra point makes it possible to draw a segment crossing the left
    // edge of the requested time window, plus a small safety margin.
    return std::max<std::size_t>(2, sample_count + 2);
}

MetricHistory::MetricHistory(std::size_t capacity)
    : samples_(capacity) {}

void MetricHistory::Push(SampleTime timestamp, MetricValue metric) {
    samples_.Push(TimestampedMetric{timestamp, metric});
}

MetricSeriesSnapshot MetricHistory::SnapshotSince(SampleTime start) const {
    MetricSeriesSnapshot result;
    const auto all_samples = samples_.Snapshot();
    result.samples.reserve(all_samples.size());

    // Include one sample immediately before the window when available. This
    // lets the renderer draw a continuous line into the visible time range.
    std::size_t first_visible = 0;
    while (first_visible < all_samples.size() && all_samples[first_visible].timestamp < start) {
        ++first_visible;
    }
    if (first_visible > 0) {
        --first_visible;
    }

    for (std::size_t index = first_visible; index < all_samples.size(); ++index) {
        result.samples.push_back(all_samples[index]);
    }

    if (!all_samples.empty()) {
        result.latest = all_samples.back().metric;
    }
    return result;
}

SampleTime MetricHistory::LatestTimestamp() const noexcept {
    const TimestampedMetric* latest = samples_.Latest();
    return latest == nullptr ? SampleTime{} : latest->timestamp;
}

HistoryStore::HistoryStore(HistorySettings settings)
    : settings_(settings),
      cpu_total_(settings.RequiredCapacity()),
      gpu_total_(settings.RequiredCapacity()),
      gpu_memory_(settings.RequiredCapacity()) {}

void HistoryStore::EnsureLogicalProcessorHistories(std::size_t count) {
    const std::size_t capacity = settings_.RequiredCapacity();
    while (cpu_logical_processors_.size() < count) {
        cpu_logical_processors_.emplace_back(capacity);
    }
}

void HistoryStore::Push(const SystemSample& sample) {
    cpu_total_.Push(sample.timestamp, sample.cpu.total_utilization);
    gpu_total_.Push(sample.timestamp, sample.gpu.total_utilization);
    gpu_memory_.Push(sample.timestamp, sample.gpu.memory_utilization);

    EnsureLogicalProcessorHistories(sample.cpu.logical_processor_utilization.size());
    for (std::size_t index = 0; index < cpu_logical_processors_.size(); ++index) {
        const MetricValue metric = index < sample.cpu.logical_processor_utilization.size()
            ? sample.cpu.logical_processor_utilization[index]
            : MetricValue::Unavailable();
        cpu_logical_processors_[index].Push(sample.timestamp, metric);
    }

    latest_timestamp_ = sample.timestamp;
    has_sample_ = true;
}

void HistoryStore::SetVisibleDuration(std::chrono::seconds duration) noexcept {
    settings_.visible_duration = std::clamp(
        duration,
        std::chrono::seconds{1},
        settings_.maximum_duration);
}

const HistorySettings& HistoryStore::Settings() const noexcept {
    return settings_;
}

PerformanceSnapshot HistoryStore::Snapshot() const {
    PerformanceSnapshot result;
    result.visible_duration = settings_.visible_duration;

    if (!has_sample_) {
        return result;
    }

    result.window_end = latest_timestamp_;
    result.window_start = latest_timestamp_ - settings_.visible_duration;
    result.cpu_total = cpu_total_.SnapshotSince(result.window_start);
    result.gpu_total = gpu_total_.SnapshotSince(result.window_start);
    result.gpu_memory = gpu_memory_.SnapshotSince(result.window_start);

    result.cpu_logical_processors.reserve(cpu_logical_processors_.size());
    for (const MetricHistory& history : cpu_logical_processors_) {
        result.cpu_logical_processors.push_back(history.SnapshotSince(result.window_start));
    }

    return result;
}

} // namespace perfmon::model
