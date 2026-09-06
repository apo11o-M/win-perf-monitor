#include "memory_provider.hpp"

#include <psapi.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace perfmon::monitoring {
namespace {

constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
constexpr DWORD kRawSmbiosProvider = 0x52534D42U; // 'RSMB'
constexpr BYTE kPhysicalMemoryArrayType = 16;
constexpr BYTE kMemoryDeviceType = 17;
constexpr BYTE kEndOfTableType = 127;
constexpr BYTE kSystemMemoryUse = 0x03;

constexpr std::array<const wchar_t*, 5> kCachedMemoryCounters{
    L"\\Memory\\Cache Bytes",
    L"\\Memory\\Modified Page List Bytes",
    L"\\Memory\\Standby Cache Core Bytes",
    L"\\Memory\\Standby Cache Normal Priority Bytes",
    L"\\Memory\\Standby Cache Reserve Bytes",
};

[[nodiscard]] bool IsFormattedCounterValid(const PDH_FMT_COUNTERVALUE& value) noexcept {
    return value.CStatus == PDH_CSTATUS_VALID_DATA ||
           value.CStatus == PDH_CSTATUS_NEW_DATA;
}

[[nodiscard]] model::MetricValue PagesToGib(
    SIZE_T pages,
    SIZE_T page_size) noexcept {
    const double gib =
        (static_cast<double>(pages) * static_cast<double>(page_size)) /
        kBytesPerGiB;
    if (!std::isfinite(gib) || gib < 0.0) {
        return model::MetricValue::Unavailable(model::MetricState::ProviderError);
    }
    return model::MetricValue::Valid(static_cast<float>(gib));
}

[[nodiscard]] std::uint16_t ReadU16(
    const BYTE* data,
    std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(data[offset]) |
           (static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(
    const BYTE* data,
    std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

[[nodiscard]] bool HasField(
    std::size_t structure_length,
    std::size_t offset,
    std::size_t field_size) noexcept {
    return offset <= structure_length &&
           field_size <= structure_length - offset;
}

[[nodiscard]] std::wstring_view FormFactorName(BYTE form_factor) noexcept {
    switch (form_factor) {
    case 0x03: return L"SIMM";
    case 0x04: return L"SIP";
    case 0x05: return L"Chip";
    case 0x06: return L"DIP";
    case 0x07: return L"ZIP";
    case 0x08: return L"Card";
    case 0x09: return L"DIMM";
    case 0x0A: return L"TSOP";
    case 0x0B: return L"Row of chips";
    case 0x0C: return L"RIMM";
    case 0x0D: return L"SODIMM";
    case 0x0E: return L"SRIMM";
    case 0x0F: return L"FB-DIMM";
    case 0x10: return L"Die";
    case 0x11: return L"CAMM";
    default: return {};
    }
}

[[nodiscard]] std::uint32_t MemoryDeviceSpeed(
    const BYTE* structure,
    std::size_t length) noexcept {
    // Prefer the configured speed to the module's rated speed. SMBIOS 3.3+
    // uses the extended DWORD fields when the legacy WORD is 0xFFFF.
    if (HasField(length, 0x20, sizeof(std::uint16_t))) {
        const std::uint16_t configured = ReadU16(structure, 0x20);
        if (configured == 0xFFFFU && HasField(length, 0x58, sizeof(std::uint32_t))) {
            return ReadU32(structure, 0x58);
        }
        if (configured != 0U) {
            return configured;
        }
    }

    if (HasField(length, 0x15, sizeof(std::uint16_t))) {
        const std::uint16_t rated = ReadU16(structure, 0x15);
        if (rated == 0xFFFFU && HasField(length, 0x54, sizeof(std::uint32_t))) {
            return ReadU32(structure, 0x54);
        }
        if (rated != 0U) {
            return rated;
        }
    }
    return 0U;
}

[[nodiscard]] model::MemoryInfo ReadMemoryHardwareInfo() noexcept {
    model::MemoryInfo result{};

    try {
        const DWORD required = GetSystemFirmwareTable(
            kRawSmbiosProvider,
            0,
            nullptr,
            0);
        constexpr std::size_t kRawSmbiosHeaderSize = 8;
        if (required < kRawSmbiosHeaderSize) {
            return result;
        }

        std::vector<BYTE> raw(required);
        const DWORD written = GetSystemFirmwareTable(
            kRawSmbiosProvider,
            0,
            raw.data(),
            required);
        if (written < kRawSmbiosHeaderSize || written > required) {
            return result;
        }

        const std::size_t declared_table_length = ReadU32(raw.data(), 4);
        const std::size_t available_table_length =
            static_cast<std::size_t>(written) - kRawSmbiosHeaderSize;
        const std::size_t table_length =
            std::min(declared_table_length, available_table_length);
        const BYTE* const table = raw.data() + kRawSmbiosHeaderSize;

        struct SmbiosStructureView {
            const BYTE* data;
            std::size_t length;
            BYTE type;
        };
        std::vector<SmbiosStructureView> structures;
        std::size_t position = 0;
        while (position + 4 <= table_length) {
            const BYTE* const structure = table + position;
            const BYTE type = structure[0];
            const std::size_t length = structure[1];
            if (length < 4 || position + length > table_length) {
                break;
            }
            structures.push_back(SmbiosStructureView{structure, length, type});

            std::size_t next = position + length;
            while (next + 1 < table_length &&
                   (table[next] != 0 || table[next + 1] != 0)) {
                ++next;
            }
            if (next + 1 >= table_length) {
                break;
            }
            position = next + 2;
            if (type == kEndOfTableType) {
                break;
            }
        }

        std::vector<std::uint16_t> system_array_handles;
        std::uint32_t total_slots = 0;
        for (const SmbiosStructureView& structure : structures) {
            if (structure.type != kPhysicalMemoryArrayType ||
                !HasField(structure.length, 0x0D, sizeof(std::uint16_t)) ||
                structure.data[0x05] != kSystemMemoryUse) {
                continue;
            }

            system_array_handles.push_back(ReadU16(structure.data, 0x02));
            const std::uint16_t count = ReadU16(structure.data, 0x0D);
            if (count != 0U && count != 0xFFFFU) {
                total_slots += count;
            }
        }

        std::uint32_t described_slots = 0;
        std::uint32_t populated_slots = 0;
        std::uint32_t common_speed = 0;
        std::wstring form_factor;
        bool mixed_form_factors = false;
        for (const SmbiosStructureView& structure : structures) {
            if (structure.type != kMemoryDeviceType ||
                !HasField(structure.length, 0x0C, sizeof(std::uint16_t)) ||
                !HasField(structure.length, 0x04, sizeof(std::uint16_t))) {
                continue;
            }

            const std::uint16_t array_handle = ReadU16(structure.data, 0x04);
            if (std::find(
                    system_array_handles.begin(),
                    system_array_handles.end(),
                    array_handle) == system_array_handles.end()) {
                continue;
            }

            ++described_slots;
            const std::uint16_t size = ReadU16(structure.data, 0x0C);
            if (size == 0U) {
                continue;
            }
            ++populated_slots;

            const std::uint32_t speed =
                MemoryDeviceSpeed(structure.data, structure.length);
            if (speed != 0U && speed != std::numeric_limits<std::uint32_t>::max()) {
                common_speed = common_speed == 0U
                    ? speed
                    : std::min(common_speed, speed);
            }

            if (HasField(structure.length, 0x0E, sizeof(BYTE))) {
                const std::wstring_view name = FormFactorName(structure.data[0x0E]);
                if (!name.empty()) {
                    if (form_factor.empty()) {
                        form_factor = name;
                    } else if (form_factor != name) {
                        mixed_form_factors = true;
                    }
                }
            }
        }

        const std::uint32_t resolved_total_slots = std::max(total_slots, described_slots);
        if (resolved_total_slots != 0U) {
            result.slots_total = resolved_total_slots;
            result.slots_used = std::min(populated_slots, resolved_total_slots);
        }
        if (common_speed != 0U) {
            result.speed_mtps = common_speed;
        }
        if (mixed_form_factors) {
            result.form_factor = L"Mixed";
        } else {
            result.form_factor = std::move(form_factor);
        }
    } catch (...) {
        // Firmware data is optional; dynamic memory monitoring can continue.
    }

    return result;
}

void MarkUnavailable(model::MemorySample& memory) noexcept {
    const model::MetricValue unavailable =
        model::MetricValue::Unavailable(model::MetricState::ProviderError);
    memory.total_utilization = unavailable;
    memory.info.used_gib = unavailable;
    memory.info.available_gib = unavailable;
    memory.info.total_gib = unavailable;
    memory.info.committed_gib = unavailable;
    memory.info.commit_limit_gib = unavailable;
    memory.info.cached_gib = unavailable;
    memory.info.paged_pool_gib = unavailable;
    memory.info.non_paged_pool_gib = unavailable;
}

} // namespace

MemoryProvider::MemoryProvider() {
    InitializeHardwareInfo();
    InitializeCacheCounters();
}

MemoryProvider::~MemoryProvider() {
    CloseCacheCounters();
}

void MemoryProvider::InitializeHardwareInfo() noexcept {
    hardware_info_ = ReadMemoryHardwareInfo();
}

void MemoryProvider::InitializeCacheCounters() noexcept {
    if (PdhOpenQueryW(nullptr, 0, &cache_query_) != ERROR_SUCCESS) {
        cache_query_ = nullptr;
        return;
    }

    for (std::size_t index = 0; index < kCachedMemoryCounters.size(); ++index) {
        if (PdhAddEnglishCounterW(
                cache_query_,
                kCachedMemoryCounters[index],
                0,
                &cache_counters_[index]) != ERROR_SUCCESS) {
            CloseCacheCounters();
            return;
        }
    }

    cache_counters_ready_ = true;
}

void MemoryProvider::CloseCacheCounters() noexcept {
    cache_counters_.fill(nullptr);
    cache_counters_ready_ = false;
    if (cache_query_ != nullptr) {
        (void)PdhCloseQuery(cache_query_);
        cache_query_ = nullptr;
    }
}

model::MetricValue MemoryProvider::ReadTaskManagerCachedGib() const noexcept {
    if (!cache_counters_ready_ || cache_query_ == nullptr) {
        return model::MetricValue::Unavailable(model::MetricState::Unsupported);
    }
    if (PdhCollectQueryData(cache_query_) != ERROR_SUCCESS) {
        return model::MetricValue::Unavailable(model::MetricState::ProviderError);
    }

    double total_bytes = 0.0;
    for (PDH_HCOUNTER counter : cache_counters_) {
        DWORD counter_type = 0;
        PDH_FMT_COUNTERVALUE value{};
        const PDH_STATUS status = PdhGetFormattedCounterValue(
            counter,
            PDH_FMT_DOUBLE,
            &counter_type,
            &value);
        if (status != ERROR_SUCCESS || !IsFormattedCounterValid(value) ||
            !std::isfinite(value.doubleValue) || value.doubleValue < 0.0) {
            return model::MetricValue::Unavailable();
        }
        total_bytes += value.doubleValue;
    }

    return model::MetricValue::Valid(
        static_cast<float>(total_bytes / kBytesPerGiB));
}

void MemoryProvider::Sample(model::SystemSample& sample) {
    sample.memory.info.speed_mtps = hardware_info_.speed_mtps;
    sample.memory.info.slots_used = hardware_info_.slots_used;
    sample.memory.info.slots_total = hardware_info_.slots_total;
    sample.memory.info.form_factor = hardware_info_.form_factor;

    PERFORMANCE_INFORMATION performance{};
    performance.cb = static_cast<DWORD>(sizeof(performance));
    if (GetPerformanceInfo(&performance, static_cast<DWORD>(sizeof(performance))) == FALSE ||
        performance.PageSize == 0 || performance.PhysicalTotal == 0) {
        MarkUnavailable(sample.memory);
        return;
    }

    model::MemoryInfo& info = sample.memory.info;
    info.total_gib = PagesToGib(performance.PhysicalTotal, performance.PageSize);
    info.available_gib = PagesToGib(performance.PhysicalAvailable, performance.PageSize);
    info.committed_gib = PagesToGib(performance.CommitTotal, performance.PageSize);
    info.commit_limit_gib = PagesToGib(performance.CommitLimit, performance.PageSize);
    info.paged_pool_gib = PagesToGib(performance.KernelPaged, performance.PageSize);
    info.non_paged_pool_gib = PagesToGib(performance.KernelNonpaged, performance.PageSize);

    if (performance.PhysicalAvailable <= performance.PhysicalTotal) {
        const SIZE_T used_pages = performance.PhysicalTotal - performance.PhysicalAvailable;
        info.used_gib = PagesToGib(used_pages, performance.PageSize);
        sample.memory.total_utilization = model::MetricValue::ValidPercentage(
            static_cast<float>(
                (static_cast<double>(used_pages) /
                 static_cast<double>(performance.PhysicalTotal)) * 100.0));
    } else {
        info.used_gib =
            model::MetricValue::Unavailable(model::MetricState::ProviderError);
        sample.memory.total_utilization =
            model::MetricValue::Unavailable(model::MetricState::ProviderError);
    }

    info.cached_gib = ReadTaskManagerCachedGib();
    if (!info.cached_gib.HasValue()) {
        // PERFORMANCE_INFORMATION::SystemCache is not an exact Task Manager
        // Cached match, but it is a documented, useful fallback when one of
        // the optional memory-list performance counters is unavailable.
        info.cached_gib = PagesToGib(performance.SystemCache, performance.PageSize);
    }
}

} // namespace perfmon::monitoring
