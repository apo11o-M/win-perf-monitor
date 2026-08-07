# Windows 11 Performance Monitor

A simple performance monitor for Windows 11 that displays CPU, GPU, RAM, and Disk usage in a small overlay window.

## Build Instructions

Requirements: Windows 11, CMake, Nvidia GPU (only)

```bash
cmake --preset windows-vs2022
cmake --build --preset windows-release
.\out\build\windows-vs2022\Release\PerformanceMonitor.exe
```

You may also find the prebuilt release binaries here: [Releases](https://github.com/apo11o-M/win-perf-monitor/releases/)
