#include "nvml_loader.hpp"

#include "win32_headers.hpp"

#include <array>
#include <sstream>
#include <string>

#ifndef PERFMON_HAS_NVML_HEADER
#define PERFMON_HAS_NVML_HEADER 0
#endif

#if PERFMON_HAS_NVML_HEADER
#include <nvml.h>
#endif

namespace {

class ModuleHandle {
public:
    explicit ModuleHandle(HMODULE module = nullptr) noexcept : module_(module) {}

    ~ModuleHandle() {
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    ModuleHandle(const ModuleHandle&) = delete;
    ModuleHandle& operator=(const ModuleHandle&) = delete;

    [[nodiscard]] HMODULE get() const noexcept { return module_; }
    [[nodiscard]] explicit operator bool() const noexcept { return module_ != nullptr; }

private:
    HMODULE module_ = nullptr;
};

[[nodiscard]] HMODULE LoadNvmlModule() {
    // Current NVIDIA drivers normally install the 64-bit NVML DLL in System32.
    // Restricting the lookup to System32 avoids loading an unexpected DLL from
    // the current working directory.
    return LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

#if PERFMON_HAS_NVML_HEADER

template <typename FunctionType>
[[nodiscard]] FunctionType Resolve(HMODULE module, const char* name) {
    return reinterpret_cast<FunctionType>(GetProcAddress(module, name));
}

using ErrorStringFunction = const char* (*)(nvmlReturn_t);

[[nodiscard]] std::string NvmlErrorText(
    nvmlReturn_t result,
    ErrorStringFunction error_string) {
    if (error_string == nullptr) {
        return "NVML error code " + std::to_string(static_cast<int>(result));
    }

    const char* text = error_string(result);
    return text != nullptr ? std::string(text) : "Unknown NVML error";
}

#endif

} // namespace

NvmlProbeResult ProbeNvml() {
    NvmlProbeResult result{};
    result.header_available = PERFMON_HAS_NVML_HEADER != 0;

#if !PERFMON_HAS_NVML_HEADER
    result.status = "nvml.h was not available when this executable was built";
    return result;
#else
    ModuleHandle module(LoadNvmlModule());
    if (!module) {
        result.status = "nvml.dll was not found in the Windows system directory";
        return result;
    }
    result.dll_loaded = true;

    using InitFn = nvmlReturn_t (*)();
    using ShutdownFn = nvmlReturn_t (*)();
    using DeviceGetCountFn = nvmlReturn_t (*)(unsigned int*);
    using DeviceGetHandleFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using DeviceGetNameFn = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
    using ErrorStringFn = ErrorStringFunction;

    const auto init = Resolve<InitFn>(module.get(), "nvmlInit_v2");
    const auto shutdown = Resolve<ShutdownFn>(module.get(), "nvmlShutdown");
    const auto device_get_count =
        Resolve<DeviceGetCountFn>(module.get(), "nvmlDeviceGetCount_v2");
    const auto device_get_handle =
        Resolve<DeviceGetHandleFn>(module.get(), "nvmlDeviceGetHandleByIndex_v2");
    const auto device_get_name =
        Resolve<DeviceGetNameFn>(module.get(), "nvmlDeviceGetName");
    const auto error_string =
        Resolve<ErrorStringFn>(module.get(), "nvmlErrorString");

    if (init == nullptr || shutdown == nullptr || device_get_count == nullptr ||
        device_get_handle == nullptr || device_get_name == nullptr) {
        result.status = "nvml.dll is present but required functions are missing";
        return result;
    }

    const nvmlReturn_t init_result = init();
    if (init_result != NVML_SUCCESS) {
        result.status = "NVML initialization failed: " +
                        NvmlErrorText(init_result, error_string);
        return result;
    }
    result.initialized = true;

    struct ShutdownGuard {
        ShutdownFn shutdown_fn;
        ~ShutdownGuard() {
            if (shutdown_fn != nullptr) {
                (void)shutdown_fn();
            }
        }
    } shutdown_guard{shutdown};

    unsigned int device_count = 0;
    const nvmlReturn_t count_result = device_get_count(&device_count);
    if (count_result != NVML_SUCCESS) {
        result.status = "Could not enumerate NVIDIA GPUs: " +
                        NvmlErrorText(count_result, error_string);
        return result;
    }

    if (device_count == 0) {
        result.status = "NVML initialized, but no NVIDIA GPU was found";
        return result;
    }

    nvmlDevice_t device = nullptr;
    const nvmlReturn_t handle_result = device_get_handle(0, &device);
    if (handle_result != NVML_SUCCESS) {
        result.status = "Could not open NVIDIA GPU 0: " +
                        NvmlErrorText(handle_result, error_string);
        return result;
    }

#if defined(NVML_DEVICE_NAME_V2_BUFFER_SIZE)
    constexpr std::size_t kDeviceNameBufferSize = NVML_DEVICE_NAME_V2_BUFFER_SIZE;
#else
    constexpr std::size_t kDeviceNameBufferSize = NVML_DEVICE_NAME_BUFFER_SIZE;
#endif
    std::array<char, kDeviceNameBufferSize> name{};
    const nvmlReturn_t name_result =
        device_get_name(device, name.data(), static_cast<unsigned int>(name.size()));
    if (name_result != NVML_SUCCESS) {
        result.status = "Could not read the NVIDIA GPU name: " +
                        NvmlErrorText(name_result, error_string);
        return result;
    }

    result.gpu_name = name.data();
    result.status = "NVML initialized successfully";
    return result;
#endif
}
