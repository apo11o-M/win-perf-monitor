#include "nvidia_gpu_provider.hpp"

#include "../text_util.hpp"
#include "../win32_headers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

#ifndef PERFMON_HAS_NVML_HEADER
#define PERFMON_HAS_NVML_HEADER 0
#endif

#if PERFMON_HAS_NVML_HEADER
#include <nvml.h>
#endif

namespace perfmon::monitoring {
namespace {

#if PERFMON_HAS_NVML_HEADER

constexpr float kBytesPerGiB = 1024.0F * 1024.0F * 1024.0F;

template <typename FunctionType>
[[nodiscard]] FunctionType Resolve(HMODULE module, const char* name) noexcept {
    return reinterpret_cast<FunctionType>(GetProcAddress(module, name));
}

[[nodiscard]] model::MetricState MetricStateForNvmlError(nvmlReturn_t result) noexcept {
    if (result == NVML_ERROR_NOT_SUPPORTED) {
        return model::MetricState::Unsupported;
    }

    if (result == NVML_ERROR_GPU_IS_LOST ||
        result == NVML_ERROR_DRIVER_NOT_LOADED ||
        result == NVML_ERROR_UNINITIALIZED) {
        return model::MetricState::ProviderError;
    }

    return model::MetricState::TemporarilyUnavailable;
}

[[nodiscard]] model::MetricValue UnavailableForNvmlError(nvmlReturn_t result) noexcept {
    return model::MetricValue::Unavailable(MetricStateForNvmlError(result));
}

#endif

} // namespace

struct NvidiaGpuProvider::Impl {
    model::GpuInfo info{};
    model::MetricState unavailable_state = model::MetricState::ProviderError;

#if PERFMON_HAS_NVML_HEADER
    using InitFn = nvmlReturn_t (*)();
    using ShutdownFn = nvmlReturn_t (*)();
    using DeviceGetCountFn = nvmlReturn_t (*)(unsigned int*);
    using DeviceGetHandleFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using DeviceGetNameFn = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
    using DeviceGetUtilizationRatesFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
    using DeviceGetMemoryInfoFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
#if defined(nvmlMemory_v2)
    using DeviceGetMemoryInfoV2Fn = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_v2_t*);
#endif
    using DeviceGetTemperatureFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
    using DeviceGetPowerUsageFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
    using DeviceGetClockInfoFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlClockType_t, unsigned int*);
    using SystemGetDriverVersionFn = nvmlReturn_t (*)(char*, unsigned int);
    using ErrorStringFn = const char* (*)(nvmlReturn_t);

    HMODULE module = nullptr;
    bool initialized = false;
    bool available = false;
    nvmlDevice_t device = nullptr;

    ShutdownFn shutdown = nullptr;
    DeviceGetUtilizationRatesFn device_get_utilization_rates = nullptr;
    DeviceGetMemoryInfoFn device_get_memory_info = nullptr;
#if defined(nvmlMemory_v2)
    DeviceGetMemoryInfoV2Fn device_get_memory_info_v2 = nullptr;
#endif
    DeviceGetTemperatureFn device_get_temperature = nullptr;
    DeviceGetPowerUsageFn device_get_power_usage = nullptr;
    DeviceGetClockInfoFn device_get_clock_info = nullptr;
    ErrorStringFn error_string = nullptr;
#endif

    Impl() {
        Initialize();
    }

    ~Impl() {
#if PERFMON_HAS_NVML_HEADER
        if (initialized && shutdown != nullptr) {
            (void)shutdown();
        }
        if (module != nullptr) {
            FreeLibrary(module);
        }
#endif
    }

    void Initialize() {
#if !PERFMON_HAS_NVML_HEADER
        unavailable_state = model::MetricState::Unsupported;
        info.provider_status = L"NVML headers unavailable at build time";
#else
        // NVIDIA installs the 64-bit NVML runtime with the display driver.
        // Restrict lookup to System32 so an unrelated DLL beside the executable
        // cannot be loaded in its place.
        module = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr) {
            info.provider_status = L"NVML runtime unavailable";
            return;
        }

        const auto init = Resolve<InitFn>(module, "nvmlInit_v2");
        shutdown = Resolve<ShutdownFn>(module, "nvmlShutdown");
        const auto device_get_count = Resolve<DeviceGetCountFn>(module, "nvmlDeviceGetCount_v2");
        const auto device_get_handle = Resolve<DeviceGetHandleFn>(module, "nvmlDeviceGetHandleByIndex_v2");
        const auto device_get_name = Resolve<DeviceGetNameFn>(module, "nvmlDeviceGetName");
        device_get_utilization_rates =
            Resolve<DeviceGetUtilizationRatesFn>(module, "nvmlDeviceGetUtilizationRates");
        device_get_memory_info =
            Resolve<DeviceGetMemoryInfoFn>(module, "nvmlDeviceGetMemoryInfo");
#if defined(nvmlMemory_v2)
        device_get_memory_info_v2 =
            Resolve<DeviceGetMemoryInfoV2Fn>(module, "nvmlDeviceGetMemoryInfo_v2");
#endif
        device_get_temperature =
            Resolve<DeviceGetTemperatureFn>(module, "nvmlDeviceGetTemperature");
        device_get_power_usage =
            Resolve<DeviceGetPowerUsageFn>(module, "nvmlDeviceGetPowerUsage");
        device_get_clock_info =
            Resolve<DeviceGetClockInfoFn>(module, "nvmlDeviceGetClockInfo");
        const auto system_get_driver_version =
            Resolve<SystemGetDriverVersionFn>(module, "nvmlSystemGetDriverVersion");
        error_string = Resolve<ErrorStringFn>(module, "nvmlErrorString");

        if (init == nullptr || shutdown == nullptr || device_get_count == nullptr ||
            device_get_handle == nullptr || device_get_name == nullptr) {
            info.provider_status = L"NVML runtime is missing required entry points";
            return;
        }

        const nvmlReturn_t init_result = init();
        if (init_result != NVML_SUCCESS) {
            info.provider_status = L"NVML initialization failed";
            return;
        }
        initialized = true;

        unsigned int device_count = 0;
        const nvmlReturn_t count_result = device_get_count(&device_count);
        if (count_result != NVML_SUCCESS) {
            info.provider_status = L"NVML could not enumerate NVIDIA GPUs";
            return;
        }
        if (device_count == 0) {
            unavailable_state = model::MetricState::Unsupported;
            info.provider_status = L"No NVIDIA GPU found";
            return;
        }

        const nvmlReturn_t handle_result = device_get_handle(0, &device);
        if (handle_result != NVML_SUCCESS || device == nullptr) {
            info.provider_status = L"NVML could not open GPU 0";
            return;
        }

#if defined(NVML_DEVICE_NAME_V2_BUFFER_SIZE)
        constexpr std::size_t kDeviceNameBufferSize = NVML_DEVICE_NAME_V2_BUFFER_SIZE;
#else
        constexpr std::size_t kDeviceNameBufferSize = NVML_DEVICE_NAME_BUFFER_SIZE;
#endif
        std::array<char, kDeviceNameBufferSize> device_name{};
        if (device_get_name(
                device,
                device_name.data(),
                static_cast<unsigned int>(device_name.size())) == NVML_SUCCESS) {
            info.gpu_name = Utf8ToWide(device_name.data());
        }

        if (system_get_driver_version != nullptr) {
            std::array<char, NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE> driver_version{};
            if (system_get_driver_version(
                    driver_version.data(),
                    static_cast<unsigned int>(driver_version.size())) == NVML_SUCCESS) {
                info.driver_version = Utf8ToWide(driver_version.data());
            }
        }

        available = true;
        unavailable_state = model::MetricState::TemporarilyUnavailable;
        info.provider_status = L"NVML active";
#endif
    }

    void MarkUnavailable(model::GpuSample& gpu) const {
        gpu.total_utilization = model::MetricValue::Unavailable(unavailable_state);
        gpu.memory_utilization = model::MetricValue::Unavailable(unavailable_state);
        gpu.info = info;
        gpu.info.memory_used_gib = model::MetricValue::Unavailable(unavailable_state);
        gpu.info.memory_total_gib = model::MetricValue::Unavailable(unavailable_state);
        gpu.info.temperature_c = model::MetricValue::Unavailable(unavailable_state);
        gpu.info.power_w = model::MetricValue::Unavailable(unavailable_state);
        gpu.info.graphics_clock_mhz = model::MetricValue::Unavailable(unavailable_state);
        gpu.info.memory_clock_mhz = model::MetricValue::Unavailable(unavailable_state);
    }

    void Sample(model::GpuSample& gpu) {
#if !PERFMON_HAS_NVML_HEADER
        MarkUnavailable(gpu);
#else
        if (!available || device == nullptr) {
            MarkUnavailable(gpu);
            return;
        }

        gpu.info = info;

        if (device_get_utilization_rates == nullptr) {
            gpu.total_utilization =
                model::MetricValue::Unavailable(model::MetricState::Unsupported);
        } else {
            nvmlUtilization_t utilization{};
            const nvmlReturn_t result = device_get_utilization_rates(device, &utilization);
            gpu.total_utilization = result == NVML_SUCCESS
                ? model::MetricValue::ValidPercentage(static_cast<float>(utilization.gpu))
                : UnavailableForNvmlError(result);
        }

        bool memory_sampled = false;
#if defined(nvmlMemory_v2)
        if (device_get_memory_info_v2 != nullptr) {
            nvmlMemory_v2_t memory{nvmlMemory_v2};
            const nvmlReturn_t result = device_get_memory_info_v2(device, &memory);
            if (result == NVML_SUCCESS && memory.total > 0) {
                // NVML memory-info v2 reports the physical framebuffer total and
                // exposes driver/firmware-reserved memory separately. Subtract the
                // reserved portion from "used" so the UI continues to represent
                // user-visible allocated VRAM rather than driver bookkeeping.
                const unsigned long long allocated_bytes =
                    memory.used >= memory.reserved ? memory.used - memory.reserved : memory.used;
                const double used = static_cast<double>(allocated_bytes);
                const double total = static_cast<double>(memory.total);
                gpu.memory_utilization = model::MetricValue::ValidPercentage(
                    static_cast<float>((used / total) * 100.0));
                gpu.info.memory_used_gib = model::MetricValue::Valid(
                    static_cast<float>(used / static_cast<double>(kBytesPerGiB)));
                gpu.info.memory_total_gib = model::MetricValue::Valid(
                    static_cast<float>(total / static_cast<double>(kBytesPerGiB)));
                memory_sampled = true;
            } else if (result != NVML_ERROR_NOT_SUPPORTED) {
                const model::MetricValue unavailable = result == NVML_SUCCESS
                    ? model::MetricValue::Unavailable(model::MetricState::TemporarilyUnavailable)
                    : UnavailableForNvmlError(result);
                gpu.memory_utilization = unavailable;
                gpu.info.memory_used_gib = unavailable;
                gpu.info.memory_total_gib = unavailable;
                memory_sampled = true;
            }
        }
#endif

        if (!memory_sampled) {
            if (device_get_memory_info == nullptr) {
                gpu.memory_utilization =
                    model::MetricValue::Unavailable(model::MetricState::Unsupported);
                gpu.info.memory_used_gib =
                    model::MetricValue::Unavailable(model::MetricState::Unsupported);
                gpu.info.memory_total_gib =
                    model::MetricValue::Unavailable(model::MetricState::Unsupported);
            } else {
                nvmlMemory_t memory{};
                const nvmlReturn_t result = device_get_memory_info(device, &memory);
                if (result == NVML_SUCCESS && memory.total > 0) {
                    const double used = static_cast<double>(memory.used);
                    const double total = static_cast<double>(memory.total);
                    gpu.memory_utilization = model::MetricValue::ValidPercentage(
                        static_cast<float>((used / total) * 100.0));
                    gpu.info.memory_used_gib = model::MetricValue::Valid(
                        static_cast<float>(used / static_cast<double>(kBytesPerGiB)));
                    gpu.info.memory_total_gib = model::MetricValue::Valid(
                        static_cast<float>(total / static_cast<double>(kBytesPerGiB)));
                } else {
                    const model::MetricValue unavailable = result == NVML_SUCCESS
                        ? model::MetricValue::Unavailable(model::MetricState::TemporarilyUnavailable)
                        : UnavailableForNvmlError(result);
                    gpu.memory_utilization = unavailable;
                    gpu.info.memory_used_gib = unavailable;
                    gpu.info.memory_total_gib = unavailable;
                }
            }
        }

        if (device_get_temperature == nullptr) {
            gpu.info.temperature_c =
                model::MetricValue::Unavailable(model::MetricState::Unsupported);
        } else {
            unsigned int temperature_c = 0;
            const nvmlReturn_t result = device_get_temperature(
                device,
                NVML_TEMPERATURE_GPU,
                &temperature_c);
            gpu.info.temperature_c = result == NVML_SUCCESS
                ? model::MetricValue::Valid(static_cast<float>(temperature_c))
                : UnavailableForNvmlError(result);
        }

        if (device_get_power_usage == nullptr) {
            gpu.info.power_w =
                model::MetricValue::Unavailable(model::MetricState::Unsupported);
        } else {
            unsigned int power_mw = 0;
            const nvmlReturn_t result = device_get_power_usage(device, &power_mw);
            gpu.info.power_w = result == NVML_SUCCESS
                ? model::MetricValue::Valid(static_cast<float>(power_mw) / 1000.0F)
                : UnavailableForNvmlError(result);
        }

        if (device_get_clock_info == nullptr) {
            gpu.info.graphics_clock_mhz =
                model::MetricValue::Unavailable(model::MetricState::Unsupported);
            gpu.info.memory_clock_mhz =
                model::MetricValue::Unavailable(model::MetricState::Unsupported);
        } else {
            unsigned int graphics_clock_mhz = 0;
            nvmlReturn_t result = device_get_clock_info(
                device,
                NVML_CLOCK_GRAPHICS,
                &graphics_clock_mhz);
            gpu.info.graphics_clock_mhz = result == NVML_SUCCESS
                ? model::MetricValue::Valid(static_cast<float>(graphics_clock_mhz))
                : UnavailableForNvmlError(result);

            unsigned int memory_clock_mhz = 0;
            result = device_get_clock_info(
                device,
                NVML_CLOCK_MEM,
                &memory_clock_mhz);
            gpu.info.memory_clock_mhz = result == NVML_SUCCESS
                ? model::MetricValue::Valid(static_cast<float>(memory_clock_mhz))
                : UnavailableForNvmlError(result);
        }
#endif
    }
};

NvidiaGpuProvider::NvidiaGpuProvider()
    : impl_(std::make_unique<Impl>()) {}

NvidiaGpuProvider::~NvidiaGpuProvider() = default;

void NvidiaGpuProvider::Sample(model::SystemSample& sample) {
    impl_->Sample(sample.gpu);
}

} // namespace perfmon::monitoring
