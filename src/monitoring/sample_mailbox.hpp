#pragma once

#include "../model/system_sample.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace perfmon::monitoring {

// Bounded single-slot handoff from the sampling thread to the UI thread.
// Publish replaces any stale pending sample. The return value tells the caller
// whether it needs to post a UI notification.
class SampleMailbox {
public:
    [[nodiscard]] bool Publish(model::SystemSample sample) {
        std::scoped_lock lock(mutex_);
        latest_ = std::move(sample);
        if (notification_pending_) {
            return false;
        }
        notification_pending_ = true;
        return true;
    }

    [[nodiscard]] std::optional<model::SystemSample> ConsumeLatest() {
        std::scoped_lock lock(mutex_);
        notification_pending_ = false;
        std::optional<model::SystemSample> result = std::move(latest_);
        latest_.reset();
        return result;
    }

private:
    std::mutex mutex_;
    std::optional<model::SystemSample> latest_{};
    bool notification_pending_ = false;
};

} // namespace perfmon::monitoring
