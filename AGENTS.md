# AGENTS.md

## Project Overview

Performance Monitor is a lightweight native Windows 11 desktop widget for at-a-glance system monitoring.

The application is implemented in C++20 with Win32, Direct2D, and DirectWrite. It currently monitors CPU, NVIDIA GPU, and physical-memory activity, retains short bounded histories for rendering, and exposes a compact component rail with expandable detail views.

Preserve the native Windows architecture unless a task explicitly requests an architectural change. Do not introduce a cross-platform GUI framework, browser-based UI, managed runtime, or other application framework merely to simplify an implementation.

Release 1.0 is an existing implementation, not a greenfield project. Prefer incremental changes that fit the current design over rewrites.

## Current Implementation Checkpoint

The current working implementation includes three component cards and expandable detail panes:

- CPU: total utilization, per-logical-processor history graphs, processor identity/topology, current speed, process/thread counts, and uptime. Logical-processor graphs use grid lines and intentionally omit per-core number labels.
- NVIDIA GPU: utilization and dedicated-VRAM history graphs, plus VRAM, temperature, power, and graphics-clock statistics when NVML supports them. Current utilization is shown beside the graph header rather than duplicated in the footer statistics.
- Memory: physical-memory utilization history; used/available, committed/limit, cached, paged/non-paged pool statistics; and SMBIOS-derived speed, slots-used, and form-factor details when firmware data is available.

The component rail supports independent CPU, GPU, and Memory visibility while preventing all components from being hidden. The context menu exposes 90%, 95%, and 100% opacity choices. Window-size presets and component visibility are persisted.

Layout uses independent `padding_left`, `padding_top`, `padding_right`, and `padding_bottom` values in `ui::LayoutMetrics`. Collapsed window height is derived from the visible card count, card heights/gaps, and the top/bottom padding; do not reintroduce a single ambiguous outer-margin value.

## Sources of Truth

Use the repository sources for different kinds of information as follows:

- `docs/performance_monitor_specification.md` defines intended product behavior, scope, architecture goals, roadmap, and release requirements.
- `README.md` is a short user-facing build/use document. Keep it concise.
- The source code is authoritative for the implementation that currently exists.
- `CMakeLists.txt` and `CMakePresets.json` are authoritative for the build configuration.

Before changing product behavior, architecture, scope, or release requirements, consult `docs/performance_monitor_specification.md`.

Do not duplicate large portions of the specification into code comments or agent-facing documentation. If the implementation and specification appear inconsistent, do not silently rewrite one to match the other. Make the smallest task-relevant change and report the discrepancy when it matters.

## Development Environment

Primary development environment:

- Windows 11 x64
- Windows 11 VirtualBox development VM
- C++20
- MSVC / Visual Studio 2022 Build Tools
- Windows 11 SDK
- CMake 3.25 or newer
- Visual Studio Code is commonly used as the editor

The development VM does not expose an NVIDIA GPU. It is suitable for compiling the complete application and testing CPU, model, settings, windowing, and general UI behavior, but it cannot validate real NVML/GPU telemetry.

Actual NVIDIA GPU behavior must be validated on a Windows host with a supported NVIDIA driver/GPU.

## Build System

CMake is the source of truth. The project uses the Visual Studio 2022 CMake generator for x64 builds.

Do not:

- switch the project to Ninja unless explicitly requested;
- hand-maintain generated `.sln` or `.vcxproj` files;
- place source-of-truth build configuration in Visual Studio project files;
- edit files under `out/build/` as source files.

Canonical configure/build commands:

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-debug
cmake --build --preset windows-release
```

Typical executables are produced at:

```text
out/build/windows-vs2022/Debug/PerformanceMonitor.exe
out/build/windows-vs2022/Release/PerformanceMonitor.exe
```

The normal release target is a Windows GUI executable with a statically linked MSVC runtime where configured by CMake.

### Windows resources and manifest

`resources/app.rc.in` is the source of the executable's embedded Windows resources, including the application manifest.

The linker is intentionally configured with `/MANIFEST:NO` because the resource script already embeds the manifest. Do not re-enable automatic linker manifest generation without also reviewing the resource setup; doing both can create duplicate `MANIFEST` resources and cause `CVT1100` / `LNK1123` build failures.

## Repository Structure

Important directories and responsibilities:

```text
src/app/         Win32 application lifecycle, MainWindow, persistent settings
src/model/       Metric state, samples, history buffers, snapshots
src/monitoring/  CPU/GPU/memory providers, sampler, cross-thread sample handoff
src/ui/          Layout, graphs, Direct2D/DirectWrite rendering, UI state
resources/       Icon, manifest, version/resource metadata
tests/           Lightweight unit/model tests
docs/            Project specification
```

Keep responsibilities within these existing boundaries unless a task has a clear reason to change them.

## Architecture and Threading Invariants

### UI thread ownership

The UI thread owns and mutates:

- the Win32 message loop and window state;
- Direct2D and DirectWrite rendering resources;
- UI interaction state;
- component history storage;
- rendering and repaint requests.

Do not call Direct2D/DirectWrite rendering code or mutate UI-owned state from the sampling thread.

### Sampling thread

`monitoring::Sampler` runs provider sampling on a background `std::jthread`.

Providers populate a `model::SystemSample`. The sampling thread must not draw directly and should not perform Win32 UI operations.

Provider failures are intentionally isolated so one optional telemetry source cannot terminate the sampler or the entire application.

Do not move potentially blocking hardware queries onto the UI thread.

### Sample handoff

`monitoring::SampleMailbox` is a bounded single-slot handoff from the sampling thread to the UI thread. Publishing a newer sample may replace a stale pending sample. A `WM_APP` notification tells the UI thread to consume the latest sample.

This behavior is deliberate:

- no unbounded telemetry queue;
- no accumulating sampling backlog;
- stale pending data may be replaced by newer data;
- history mutation remains on the UI thread.

Do not replace this with an unbounded queue or cross-thread history mutation unless explicitly required by the task.

### Sampling deadlines

The sampler skips missed deadlines instead of replaying historical work. Preserve this bounded behavior. Monitoring should represent current/recent state rather than attempt to catch up by generating a backlog.

## Data Model and Metric Availability

Use the existing `model::MetricValue` / `model::MetricState` representation for telemetry availability.

The model distinguishes:

- `NotYetSampled`
- `Valid`
- `TemporarilyUnavailable`
- `Unsupported`
- `ProviderError`

Do not collapse all unavailable states into a numeric zero. Zero is a valid telemetry value and is semantically different from unavailable or unsupported data.

Use existing percentage clamping and availability helpers when appropriate. Avoid NaN/infinity reaching the renderer.

History storage is intentionally bounded. Do not introduce unbounded growth for time-series samples.

## Release 1.0 Sampling and History Behavior

Release 1.0 exposes:

- a fixed 1-second sampling interval;
- a fixed 60-second displayed history;
- no user-facing sampling-interval setting;
- no user-facing history-duration setting.

Internal classes currently retain some generalized interval/history-duration functionality. Do not remove that internal flexibility solely because Release 1.0 does not expose controls for it.

Do not add sampling/history controls to the context menu or persisted user settings unless a task explicitly changes the Release 1.0 behavior.

## Monitoring Provider Rules

### CPU

Prefer Windows-provided APIs and counters for CPU telemetry. The CPU provider should remain independent of optional NVIDIA functionality.

CPU temperature and CPU package power are outside the current core provider scope unless explicitly requested.

### NVIDIA / NVML

NVML is an optional runtime provider.

Important constraints:

- Do not link `nvml.lib` as a mandatory application dependency.
- NVML functions are resolved dynamically at runtime.
- The NVIDIA driver supplies the runtime `nvml.dll`; do not bundle a separate copy beside the executable.
- The current loader restricts `nvml.dll` lookup to the Windows system directory using `LOAD_LIBRARY_SEARCH_SYSTEM32`. Preserve the secure loading behavior.
- Missing NVML headers at build time must remain a supported configuration; the application can compile with NVML probing disabled.
- Missing NVML runtime support, unsupported metrics, and temporary NVML failures must degrade gracefully.
- CPU monitoring and the rest of the application must continue running when NVIDIA telemetry is unavailable.

Do not assume that every NVML metric exists on every NVIDIA GPU/driver combination.

### Memory

The memory provider uses native Windows facilities and keeps Windows queries off the renderer/UI thread:

- `GetPerformanceInfo` supplies physical, commit, kernel-pool, and fallback system-cache values.
- The Task Manager-oriented Cached value combines the relevant PDH cache, standby-list, and modified-page-list counters when available.
- `GetSystemFirmwareTable` reads SMBIOS Physical Memory Array and Memory Device records for configured speed, used/total slots, and form factor.

SMBIOS hardware information is static for the application lifetime and is read once during provider initialization, not once per sample. Scope Type 17 Memory Device records to their owning Type 16 system-memory arrays so unrelated firmware memory records are not counted as DIMMs.

Firmware and individual memory counters may be missing or incomplete, especially in virtual machines. Preserve independent availability states, display unavailable values as `—`, and keep dynamic physical-memory monitoring operational when optional SMBIOS or detailed cache data is unavailable.

## Rendering and UI Rules

The program is intentionally a lightweight desktop widget, not a high-frame-rate application.

Preserve these properties:

- no continuous 60 FPS render loop;
- repaint primarily when new samples arrive or UI/window state changes;
- Direct2D for graphics and DirectWrite for text;
- DPI-aware layout expressed in logical/DPI-independent units where appropriate;
- device-loss/resource recreation behavior;
- bounded rendering/history work;
- responsive UI even when a telemetry provider is slow or unavailable.

The small component graph and expanded graph should continue to consume the same underlying history data rather than maintaining independent histories.

Avoid major visual redesigns unless the task explicitly requests them. For small UI changes, follow the existing layout and rendering abstractions.

`Renderer::DrawCompactStat` is shared by CPU, GPU, and Memory detail footers. Its label layout rectangle must remain tall enough for font descenders such as `p`, `g`, and `y`; do not tighten the label/value spacing without visually checking those glyphs.

## Window and Settings Behavior

Release 1.0 persists small user settings in the current user's Windows Registry. Start-with-Windows uses the current user's standard `Run` key.

Existing persisted behavior includes items such as:

- window position;
- always-on-top state;
- opacity;
- component visibility;
- window-size preset;
- start-with-Windows preference.

The supported opacity values are currently 90, 95, and 100 percent. Older persisted 60/80-percent values are migrated to 90 percent during settings sanitization.

No administrator privileges should be required.

Invalid or unavailable settings should fail safely and fall back to sensible defaults rather than preventing startup.

Keep the application portable: do not require a configuration file adjacent to the executable unless the product requirements are explicitly changed.

## C++ and Windows Conventions

Match the style already present in the repository rather than performing unrelated formatting or naming changes.

Current conventions include:

- namespaces such as `perfmon`, `perfmon::model`, `perfmon::monitoring`, and `perfmon::ui`;
- classes, structs, enums, and member functions in `PascalCase`;
- local variables and parameters in `snake_case`;
- private data members with a trailing underscore;
- constants generally using `kPascalCase`;
- `[[nodiscard]]` where ignoring a return value would be suspicious;
- `noexcept` where the operation is intentionally non-throwing;
- RAII for ownership and cleanup;
- `Microsoft::WRL::ComPtr` for COM interface ownership;
- deleted copy operations for resource-owning/non-copyable classes;
- C++20 standard library facilities where they naturally fit.

Prefer clear ownership and scoped lifetime over manual cleanup scattered across control flow.

### Windows headers

Use `src/win32_headers.hpp` where practical for Windows API inclusion. It centralizes important Windows header behavior such as `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, Unicode configuration, and commonly used PDH headers.

Do not introduce ad-hoc Windows macro workarounds throughout unrelated translation units when the shared wrapper is the appropriate place.

The project is Unicode-first. Prefer wide Win32 APIs and existing text-conversion helpers where needed.

## Dependencies

Prefer the C++ standard library and native Windows APIs already used by the project.

Before adding a third-party dependency, verify that it provides enough value to justify increasing build, deployment, or maintenance complexity.

Do not introduce a dependency merely to replace a small amount of straightforward existing Win32/C++ code.

Runtime dependencies should remain compatible with the single-portable-executable goal. Windows system DLLs and the NVIDIA-driver-provided NVML runtime are expected exceptions.

## Change Guidelines

For normal implementation tasks:

1. Read the relevant existing implementation before editing it.
2. Consult the project specification when behavior, architecture, or scope is involved.
3. Identify the smallest subsystem that owns the requested behavior.
4. Reuse existing abstractions before creating parallel ones.
5. Make the smallest coherent change that solves the task.
6. Avoid unrelated refactors, style rewrites, file renames, or dependency changes.
7. Preserve existing user-visible behavior unless the task intentionally changes it.
8. Build/test the affected code.
9. Review the final diff for accidental changes.

Do not silently broaden the requested scope.

If an architectural or user-visible behavior change intentionally changes the specification, update the relevant specification section as part of the same change when appropriate. Routine implementation details and bug fixes do not require specification churn.

## Testing and Validation

At minimum, compile the affected target after meaningful source changes when the environment permits it.

For ordinary application changes, prefer a Debug build during iteration:

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-debug
```

Build Release when changing build configuration, packaging/resources, optimization-sensitive code, or when validating a release-oriented task:

```powershell
cmake --build --preset windows-release
```

### Unit tests

Lightweight model tests are controlled by `PERFMON_BUILD_TESTS`.

A separate test build can be configured with:

```powershell
cmake -S . -B out/build/windows-vs2022-tests `
    -G "Visual Studio 17 2022" -A x64 `
    -DPERFMON_BUILD_TESTS=ON
cmake --build out/build/windows-vs2022-tests --config Debug
ctest --test-dir out/build/windows-vs2022-tests -C Debug --output-on-failure
```

Run relevant tests when modifying covered model/history behavior, and add focused tests when a change introduces logic that is practical to test without the GUI or physical hardware.

### VM validation limitations

The Windows 11 development VM has no NVIDIA GPU access. A successful build in the VM validates compilation and non-GPU code paths, but it does not prove that:

- NVML initialized successfully;
- GPU utilization/VRAM values are correct;
- NVIDIA driver-specific behavior works on the host.

Do not report GPU runtime validation as successful unless it was actually performed on a machine with accessible NVIDIA hardware.

For telemetry changes, broad runtime comparisons against Windows Task Manager and, for NVIDIA data, `nvidia-smi` are appropriate when running on the target host.

## Completion Expectations

When finishing a coding task, report concisely:

- what behavior changed;
- which files were materially changed;
- what build/tests were run and whether they passed;
- any validation that could not be performed in the current environment;
- any important remaining issue directly related to the task.

Do not claim tests, hardware validation, or builds were performed when they were not.
