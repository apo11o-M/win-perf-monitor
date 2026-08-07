#pragma once

#include "component_provider.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace perfmon::monitoring {

class Sampler {
public:
    using PublishCallback = std::function<void(model::SystemSample)>;

    Sampler(std::chrono::milliseconds interval, PublishCallback publish_callback);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    void AddProvider(std::unique_ptr<ComponentProvider> provider);
    void Start();
    void Stop() noexcept;
    void SetInterval(std::chrono::milliseconds interval);

private:
    void Run(std::stop_token stop_token);
    [[nodiscard]] std::chrono::milliseconds Interval() const;

    mutable std::mutex settings_mutex_;
    std::condition_variable wake_condition_;
    std::chrono::milliseconds interval_{1000};
    PublishCallback publish_callback_{};
    std::vector<std::unique_ptr<ComponentProvider>> providers_{};
    std::jthread worker_{};
};

} // namespace perfmon::monitoring
