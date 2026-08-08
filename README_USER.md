# Performance Monitor

A small Windows 11 desktop widget for monitoring CPU and NVIDIA GPU usage.

## Features

- CPU utilization with per-logical-processor graphs
- CPU speed, process/thread counts, and system uptime
- NVIDIA GPU utilization, VRAM usage, temperature, power, and clock speed
- Expandable CPU and GPU detail views
- Always-on-top, opacity, component visibility, and window-size settings
- Saved window position and optional Start with Windows
- One-second updates with 60 seconds of history

## Using it

Run `PerformanceMonitor.exe`.

- Click **CPU** or **GPU** to expand its detail view.
- Click the selected component again to collapse it.
- Drag an empty area to move the widget.
- Right-click anywhere on the widget for settings or Exit.

Settings are saved automatically for the current Windows user.

## Building

Requirements:

- Windows 11
- CMake 3.25+
- Visual Studio 2022 Build Tools with MSVC and the Windows SDK
- NVIDIA CUDA Toolkit headers if building with NVIDIA GPU monitoring support

```bash
cmake --preset windows-vs2022
cmake --build --preset windows-release
```

The release executable is created at:

```text
out\build\windows-vs2022\Release\PerformanceMonitor.exe
```

NVML is loaded dynamically from the installed NVIDIA driver; `nvml.lib` is not required.
