#pragma once

#include "component_provider.hpp"

#include "../win32_headers.hpp"

#include <array>

namespace perfmon::monitoring {

class MemoryProvider final : public ComponentProvider {
public:
    MemoryProvider();
    ~MemoryProvider() override;

    MemoryProvider(const MemoryProvider&) = delete;
    MemoryProvider& operator=(const MemoryProvider&) = delete;

    void Sample(model::SystemSample& sample) override;

private:
    void InitializeHardwareInfo() noexcept;
    void InitializeCacheCounters() noexcept;
    void CloseCacheCounters() noexcept;
    [[nodiscard]] model::MetricValue ReadTaskManagerCachedGib() const noexcept;

    PDH_HQUERY cache_query_ = nullptr;
    std::array<PDH_HCOUNTER, 5> cache_counters_{};
    bool cache_counters_ready_ = false;
    model::MemoryInfo hardware_info_{};
};

} // namespace perfmon::monitoring
