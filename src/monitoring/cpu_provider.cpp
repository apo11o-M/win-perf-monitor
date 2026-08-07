#include "cpu_provider.hpp"

#include <intrin.h>
#include <powerbase.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace perfmon::monitoring {
namespace {

constexpr wchar_t kTotalUtilityCounter[] =
    L"\\Processor Information(_Total)\\% Processor Utility";
constexpr wchar_t kTotalPerformanceCounter[] =
    L"\\Processor Information(_Total)\\% Processor Performance";

// PROCESSOR_POWER_INFORMATION has historically been omitted from some SDK
// headers. Keep a private layout-compatible definition so the provider does
// not depend on that typedef being present. CallNtPowerInformation treats the
// output as an opaque byte buffer.
struct ProcessorPowerInformationRecord {
    ULONG number = 0;
    ULONG max_mhz = 0;
    ULONG current_mhz = 0;
    ULONG mhz_limit = 0;
    ULONG max_idle_state = 0;
    ULONG current_idle_state = 0;
};

[[nodiscard]] bool IsFormattedCounterValid(const PDH_FMT_COUNTERVALUE& value) noexcept {
    return value.CStatus == PDH_CSTATUS_VALID_DATA ||
           value.CStatus == PDH_CSTATUS_NEW_DATA;
}

[[nodiscard]] std::wstring TrimAsciiBrandString(const char* text) {
    if (text == nullptr) {
        return {};
    }

    std::string value{text};
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();

    if (first >= last) {
        return {};
    }

    value = std::string(first, last);
    return std::wstring(value.begin(), value.end());
}

} // namespace

CpuProvider::CpuProvider() {
    InitializeStaticInfo();
    InitializePdh();
}

CpuProvider::~CpuProvider() {
    ClosePdh();
}

void CpuProvider::InitializeStaticInfo() {
    static_info_.processor_name = QueryProcessorName();

    const std::uint32_t physical_cores = QueryPhysicalCoreCount();
    if (physical_cores != 0) {
        static_info_.physical_core_count = physical_cores;
    }

    logical_processors_ = EnumerateLogicalProcessors();
    nominal_speed_ghz_ = QueryNominalSpeedGhz(logical_processors_.size());
    if (!logical_processors_.empty()) {
        static_info_.logical_processor_count =
            static_cast<std::uint32_t>(logical_processors_.size());
    }
}

void CpuProvider::InitializePdh() {
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
        query_ = nullptr;
        return;
    }

    if (PdhAddEnglishCounterW(
            query_,
            kTotalUtilityCounter,
            0,
            &total_counter_) != ERROR_SUCCESS) {
        total_counter_ = nullptr;
    }

    if (PdhAddEnglishCounterW(
            query_,
            kTotalPerformanceCounter,
            0,
            &total_performance_counter_) != ERROR_SUCCESS) {
        total_performance_counter_ = nullptr;
    }

    logical_counters_.reserve(logical_processors_.size());
    for (const LogicalProcessorAddress& processor : logical_processors_) {
        const std::wstring path =
            L"\\Processor Information(" +
            std::to_wstring(processor.group) + L"," +
            std::to_wstring(processor.number) +
            L")\\% Processor Utility";

        PDH_HCOUNTER counter = nullptr;
        if (PdhAddEnglishCounterW(query_, path.c_str(), 0, &counter) != ERROR_SUCCESS) {
            counter = nullptr;
        }
        logical_counters_.push_back(counter);
    }

    pdh_ready_ = total_counter_ != nullptr ||
        total_performance_counter_ != nullptr ||
        std::any_of(logical_counters_.begin(), logical_counters_.end(), [](PDH_HCOUNTER counter) {
            return counter != nullptr;
        });

    if (!pdh_ready_) {
        ClosePdh();
    }
}

void CpuProvider::ClosePdh() noexcept {
    logical_counters_.clear();
    total_counter_ = nullptr;
    total_performance_counter_ = nullptr;
    pdh_ready_ = false;
    has_previous_collection_ = false;

    if (query_ != nullptr) {
        (void)PdhCloseQuery(query_);
        query_ = nullptr;
    }
}

model::MetricValue CpuProvider::ReadUnboundedCounter(PDH_HCOUNTER counter) const noexcept {
    if (counter == nullptr) {
        return model::MetricValue::Unavailable(model::MetricState::Unsupported);
    }

    DWORD counter_type = 0;
    PDH_FMT_COUNTERVALUE value{};
    const PDH_STATUS status = PdhGetFormattedCounterValue(
        counter,
        PDH_FMT_DOUBLE,
        &counter_type,
        &value);

    if (status != ERROR_SUCCESS) {
        return model::MetricValue::Unavailable(model::MetricState::ProviderError);
    }
    if (!IsFormattedCounterValid(value)) {
        return model::MetricValue::Unavailable();
    }

    return model::MetricValue::Valid(static_cast<float>(value.doubleValue));
}

model::MetricValue CpuProvider::ReadPercentageCounter(PDH_HCOUNTER counter) const noexcept {
    const model::MetricValue raw_value = ReadUnboundedCounter(counter);
    if (!raw_value.HasValue()) {
        return raw_value;
    }
    return model::MetricValue::ValidPercentage(raw_value.value);
}

void CpuProvider::Sample(model::SystemSample& sample) {
    sample.cpu.info = static_info_;
    sample.cpu.info.system_uptime =
        std::chrono::milliseconds{static_cast<std::int64_t>(GetTickCount64())};
    const SystemObjectCounts object_counts = QuerySystemObjectCounts();
    sample.cpu.info.process_count = object_counts.process_count;
    sample.cpu.info.thread_count = object_counts.thread_count;

    sample.cpu.logical_processor_utilization.assign(
        logical_processors_.size(),
        model::MetricValue::Unavailable(model::MetricState::ProviderError));

    if (!pdh_ready_ || query_ == nullptr) {
        sample.cpu.total_utilization =
            model::MetricValue::Unavailable(model::MetricState::ProviderError);
        sample.cpu.info.current_speed_ghz =
            model::MetricValue::Unavailable(model::MetricState::ProviderError);
        return;
    }

    const PDH_STATUS collect_status = PdhCollectQueryData(query_);
    if (collect_status != ERROR_SUCCESS) {
        sample.cpu.total_utilization =
            model::MetricValue::Unavailable(model::MetricState::ProviderError);
        sample.cpu.info.current_speed_ghz =
            model::MetricValue::Unavailable(model::MetricState::ProviderError);
        return;
    }

    // Processor Utility is a rate-style counter. The first collection primes
    // PDH's previous-sample state; a displayable value is available from the
    // next scheduled collection onward.
    if (!has_previous_collection_) {
        has_previous_collection_ = true;
        sample.cpu.total_utilization = model::MetricValue::Unavailable();
        std::fill(
            sample.cpu.logical_processor_utilization.begin(),
            sample.cpu.logical_processor_utilization.end(),
            model::MetricValue::Unavailable());
        sample.cpu.info.current_speed_ghz = model::MetricValue::Unavailable();
        return;
    }

    sample.cpu.total_utilization = ReadPercentageCounter(total_counter_);

    const model::MetricValue performance_percent =
        ReadUnboundedCounter(total_performance_counter_);
    if (nominal_speed_ghz_.HasValue() && performance_percent.HasValue()) {
        const float performance_ratio = std::max(0.0F, performance_percent.value) / 100.0F;
        sample.cpu.info.current_speed_ghz = model::MetricValue::Valid(
            nominal_speed_ghz_.value * performance_ratio);
    } else if (!nominal_speed_ghz_.HasValue()) {
        sample.cpu.info.current_speed_ghz = nominal_speed_ghz_;
    } else {
        sample.cpu.info.current_speed_ghz = performance_percent;
    }

    float logical_sum = 0.0F;
    std::size_t valid_logical_count = 0;
    for (std::size_t index = 0; index < logical_counters_.size(); ++index) {
        model::MetricValue value = ReadPercentageCounter(logical_counters_[index]);
        sample.cpu.logical_processor_utilization[index] = value;
        if (value.HasValue()) {
            logical_sum += value.value;
            ++valid_logical_count;
        }
    }

    // The _Total counter should normally exist. If it is temporarily missing
    // but individual processors are valid, preserve a useful aggregate rail
    // value by averaging the available logical-processor utilities.
    if (!sample.cpu.total_utilization.HasValue() && valid_logical_count != 0) {
        sample.cpu.total_utilization = model::MetricValue::ValidPercentage(
            logical_sum / static_cast<float>(valid_logical_count));
    }
}

std::wstring CpuProvider::QueryProcessorName() {
    std::array<int, 4> registers{};
    __cpuid(registers.data(), static_cast<int>(0x80000000U));
    const unsigned int maximum_extended_leaf =
        static_cast<unsigned int>(registers[0]);

    if (maximum_extended_leaf < 0x80000004U) {
        return {};
    }

    std::array<char, 49> brand{};
    for (unsigned int leaf_index = 0; leaf_index < 3; ++leaf_index) {
        __cpuid(registers.data(), static_cast<int>(0x80000002U + leaf_index));
        std::memcpy(
            brand.data() + (leaf_index * 16U),
            registers.data(),
            16U);
    }
    brand.back() = '\0';

    return TrimAsciiBrandString(brand.data());
}

std::uint32_t CpuProvider::QueryPhysicalCoreCount() noexcept {
    DWORD required_bytes = 0;
    if (GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            nullptr,
            &required_bytes) != FALSE) {
        return 0;
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required_bytes == 0) {
        return 0;
    }

    try {
        std::vector<unsigned char> buffer(required_bytes);
        auto* information = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            buffer.data());
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                information,
                &required_bytes) == FALSE) {
            return 0;
        }

        std::uint32_t core_count = 0;
        DWORD offset = 0;
        while (offset < required_bytes) {
            const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                buffer.data() + offset);
            if (entry->Size == 0 || entry->Size > required_bytes - offset) {
                return 0;
            }
            if (entry->Relationship == RelationProcessorCore) {
                ++core_count;
            }
            offset += entry->Size;
        }
        return core_count;
    } catch (...) {
        return 0;
    }
}

model::MetricValue CpuProvider::QueryNominalSpeedGhz(std::size_t processor_count) noexcept {
    if (processor_count == 0) {
        return model::MetricValue::Unavailable(model::MetricState::Unsupported);
    }

    try {
        std::vector<ProcessorPowerInformationRecord> power_info(processor_count);
        const std::size_t output_bytes = power_info.size() * sizeof(ProcessorPowerInformationRecord);
        if (output_bytes > static_cast<std::size_t>(std::numeric_limits<ULONG>::max())) {
            return model::MetricValue::Unavailable(model::MetricState::ProviderError);
        }

        const auto status = CallNtPowerInformation(
            ProcessorInformation,
            nullptr,
            0,
            power_info.data(),
            static_cast<ULONG>(output_bytes));
        if (status != 0) {
            return model::MetricValue::Unavailable(model::MetricState::ProviderError);
        }

        std::uint64_t total_mhz = 0;
        std::size_t valid_count = 0;
        for (const ProcessorPowerInformationRecord& processor : power_info) {
            if (processor.max_mhz == 0) {
                continue;
            }
            total_mhz += processor.max_mhz;
            ++valid_count;
        }

        if (valid_count == 0) {
            return model::MetricValue::Unavailable();
        }

        const float average_mhz =
            static_cast<float>(total_mhz) / static_cast<float>(valid_count);
        return model::MetricValue::Valid(average_mhz / 1000.0F);
    } catch (...) {
        return model::MetricValue::Unavailable(model::MetricState::ProviderError);
    }
}

CpuProvider::SystemObjectCounts CpuProvider::QuerySystemObjectCounts() noexcept {
    PERFORMANCE_INFORMATION performance{};
    performance.cb = static_cast<DWORD>(sizeof(performance));

    if (GetPerformanceInfo(&performance, static_cast<DWORD>(sizeof(performance))) == FALSE) {
        return {};
    }

    SystemObjectCounts result;
    result.process_count = static_cast<std::uint32_t>(performance.ProcessCount);
    result.thread_count = static_cast<std::uint32_t>(performance.ThreadCount);
    return result;
}

std::vector<CpuProvider::LogicalProcessorAddress> CpuProvider::EnumerateLogicalProcessors() {
    std::vector<LogicalProcessorAddress> processors;

    const WORD group_count = GetActiveProcessorGroupCount();
    for (WORD group = 0; group < group_count; ++group) {
        const DWORD processor_count = GetActiveProcessorCount(group);
        if (processor_count == 0) {
            continue;
        }

        for (DWORD processor = 0; processor < processor_count; ++processor) {
            processors.push_back(LogicalProcessorAddress{group, processor});
        }
    }

    return processors;
}

} // namespace perfmon::monitoring
