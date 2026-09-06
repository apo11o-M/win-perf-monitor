# Windows 11 Performance Monitor

A simple performance monitor for Windows 11 that displays CPU, NVIDIA GPU, and memory usage in a small overlay window.

## Build Instructions

Requirements: Windows 11 and CMake. NVIDIA GPU monitoring is optional.

```bash
cmake --preset windows-vs2022
cmake --build --preset windows-release
.\out\build\windows-vs2022\Release\PerformanceMonitor.exe
```

NVML is loaded dynamically from the installed NVIDIA driver; `nvml.lib` is not required.

You may also find the prebuilt release binaries here: [Releases](https://github.com/apo11o-M/win-perf-monitor/releases/)
