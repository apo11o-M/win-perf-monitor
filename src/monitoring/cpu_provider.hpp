#pragma once

#include "component_provider.hpp"

#include "../win32_headers.hpp"

#include <pdh.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace perfmon::monitoring {

class CpuProvider final : public ComponentProvider {
public:
    CpuProvider();
    ~CpuProvider() override;

    CpuProvider(const CpuProvider&) = delete;
    CpuProvider& operator=(const CpuProvider&) = delete;

    void Sample(model::SystemSample& sample) override;

private:
    struct LogicalProcessorAddress {
        WORD group = 0;
        DWORD number = 0;
    };

    void InitializeStaticInfo();
    void InitializePdh();
    void ClosePdh() noexcept;

    [[nodiscard]] model::MetricValue ReadPercentageCounter(PDH_HCOUNTER counter) const noexcept;
    [[nodiscard]] model::MetricValue ReadUnboundedCounter(PDH_HCOUNTER counter) const noexcept;
    struct SystemObjectCounts {
        std::optional<std::uint32_t> process_count{};
        std::optional<std::uint32_t> thread_count{};
    };

    [[nodiscard]] static std::wstring QueryProcessorName();
    [[nodiscard]] static std::uint32_t QueryPhysicalCoreCount() noexcept;
    [[nodiscard]] static std::vector<LogicalProcessorAddress> EnumerateLogicalProcessors();
    [[nodiscard]] static model::MetricValue QueryNominalSpeedGhz(std::size_t processor_count) noexcept;
    [[nodiscard]] static SystemObjectCounts QuerySystemObjectCounts() noexcept;

    model::CpuInfo static_info_{};
    std::vector<LogicalProcessorAddress> logical_processors_{};

    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER total_counter_ = nullptr;
    PDH_HCOUNTER total_performance_counter_ = nullptr;
    std::vector<PDH_HCOUNTER> logical_counters_{};
    model::MetricValue nominal_speed_ghz_{};
    bool pdh_ready_ = false;
    bool has_previous_collection_ = false;
};

} // namespace perfmon::monitoring
