#pragma once

#include "component_provider.hpp"

#include <memory>

namespace perfmon::monitoring {

// NVIDIA GPU telemetry provider backed by NVML. NVML is loaded dynamically at
// runtime so PerformanceMonitor has no link-time dependency on nvml.lib and can
// continue running CPU monitoring when the NVIDIA library is unavailable.
class NvidiaGpuProvider final : public ComponentProvider {
public:
    NvidiaGpuProvider();
    ~NvidiaGpuProvider() override;

    NvidiaGpuProvider(const NvidiaGpuProvider&) = delete;
    NvidiaGpuProvider& operator=(const NvidiaGpuProvider&) = delete;

    void Sample(model::SystemSample& sample) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace perfmon::monitoring
