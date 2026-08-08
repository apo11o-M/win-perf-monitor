#include "sampler.hpp"

#include <algorithm>
#include <stdexcept>

namespace perfmon::monitoring {

Sampler::Sampler(
    std::chrono::milliseconds interval,
    PublishCallback publish_callback)
    : interval_(std::max(interval, std::chrono::milliseconds{1})),
      publish_callback_(std::move(publish_callback)) {
    if (!publish_callback_) {
        throw std::invalid_argument("Sampler requires a publish callback");
    }
}

Sampler::~Sampler() {
    Stop();
}

void Sampler::AddProvider(std::unique_ptr<ComponentProvider> provider) {
    if (worker_.joinable()) {
        throw std::logic_error("Cannot add providers after sampler start");
    }
    if (provider) {
        providers_.push_back(std::move(provider));
    }
}

void Sampler::Start() {
    if (worker_.joinable()) {
        return;
    }
    worker_ = std::jthread([this](std::stop_token stop_token) {
        Run(stop_token);
    });
}

void Sampler::Stop() noexcept {
    if (!worker_.joinable()) {
        return;
    }
    worker_.request_stop();
    wake_condition_.notify_all();
    worker_.join();
}

void Sampler::SetInterval(std::chrono::milliseconds interval) {
    {
        std::scoped_lock lock(settings_mutex_);
        interval_ = std::max(interval, std::chrono::milliseconds{1});
    }
    wake_condition_.notify_all();
}

std::chrono::milliseconds Sampler::Interval() const {
    std::scoped_lock lock(settings_mutex_);
    return interval_;
}

void Sampler::Run(std::stop_token stop_token) {
    auto next_deadline = model::SampleClock::now();

    while (!stop_token.stop_requested()) {
        model::SystemSample sample{};
        sample.timestamp = model::SampleClock::now();
        for (const auto& provider : providers_) {
            try {
                provider->Sample(sample);
            } catch (...) {
                // A provider is optional telemetry. One unexpected provider
                // failure must not terminate the sampling thread or the app.
            }
        }

        try {
            publish_callback_(std::move(sample));
        } catch (...) {
            // Keep the sampler alive if a future publish implementation throws.
        }

        const auto interval = Interval();
        next_deadline += interval;

        // If collection or UI work caused us to miss one or more deadlines,
        // skip them. We never replay a backlog of historical samples.
        const auto now = model::SampleClock::now();
        if (next_deadline <= now) {
            const auto overdue = now - next_deadline;
            const auto missed = (overdue / interval) + 1;
            next_deadline += interval * missed;
        }

        std::unique_lock lock(settings_mutex_);
        const auto observed_interval = interval_;
        const bool interrupted = wake_condition_.wait_until(
            lock,
            next_deadline,
            [&] {
                return stop_token.stop_requested() || interval_ != observed_interval;
            });

        if (stop_token.stop_requested()) {
            break;
        }

        // An interval change should take effect immediately instead of waiting
        // for a deadline calculated with the old cadence.
        if (interrupted && interval_ != observed_interval) {
            next_deadline = model::SampleClock::now();
        }
    }
}

} // namespace perfmon::monitoring
