#pragma once

#include <string>

struct NvmlProbeResult {
    bool header_available = false;
    bool dll_loaded = false;
    bool initialized = false;
    std::string gpu_name;
    std::string status;
};

// Performs a minimal, one-time NVML probe. The implementation loads nvml.dll
// at runtime and never creates a link-time dependency on nvml.lib.
[[nodiscard]] NvmlProbeResult ProbeNvml();
