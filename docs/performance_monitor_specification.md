# Performance Monitor — Project Specification

**Project name:** Performance Monitor  
**Document status:** Initial specification  
**Target platform:** Windows 11  
**Primary user:** Rick  
**Initial hardware target:** AMD Ryzen 9 5900X, NVIDIA GeForce RTX 5070, 64 GB RAM, two NVMe SSDs  
**Primary implementation language:** C++20  
**Build system:** CMake  
**Last updated:** August 7, 2026

---

## 1. Project Overview

Performance Monitor is a lightweight Windows desktop widget for viewing the current load and recent utilization history of major computer components.

The application is inspired by the **Performance** tab in Windows 11 Task Manager, but it is intentionally smaller, simpler, and optimized for quick at-a-glance monitoring rather than detailed system diagnosis or profiling.

The application will initially focus on:

- CPU utilization
- NVIDIA GPU utilization and related GPU statistics

Memory and disk monitoring will be added in later phases.

The program is intended for personal use on one known desktop configuration. Broad hardware compatibility is desirable when it is inexpensive to support, but it is not a requirement for the initial versions.

---

## 2. Project Goals

The primary goals are:

1. Provide a compact, always-available view of current CPU and GPU load.
2. Show recent utilization history using continuously scrolling graphs.
3. Allow a component to be expanded for additional graphs and statistics.
4. Use a borderless floating-window design with minimal screen usage.
5. Produce a single portable executable.
6. Keep runtime CPU, GPU, and memory overhead low.
7. Use a native C++ Windows implementation.
8. Use CMake and VS Code rather than manually maintained Visual Studio project files.
9. Keep data collection, application state, and rendering cleanly separated.
10. Build the project incrementally, beginning with CPU and GPU monitoring.

---

## 3. Non-Goals

The following are outside the initial scope:

- Replacing Windows Task Manager
- Per-process performance monitoring
- Performance profiling of individual programs
- High-frequency or measurement-grade telemetry
- Remote monitoring
- Historical logging to disk
- Cloud synchronization
- Multi-user support
- Broad compatibility with arbitrary hardware configurations
- Cross-platform support
- CPU temperature monitoring
- Motherboard voltage, fan, or sensor monitoring
- Overclocking or hardware control
- Alerts and notifications
- Network utilization monitoring
- Multiple simultaneous expanded component panes
- A plugin system
- A full installer

These items may be reconsidered after the core application is stable.

---

## 4. Target Environment

### 4.1 Operating System

- Windows 11
- 64-bit x86 target
- Per-monitor DPI awareness
- Normal user privileges
- No administrator privileges required for the initial metric set

### 4.2 Initial Hardware

- CPU: AMD Ryzen 9 5900X
- CPU topology: 12 physical cores, 24 logical processors
- Memory: 64 GB, installed as 2 × 32 GB
- GPU: NVIDIA GeForce RTX 5070
- Storage: two NVMe SSDs

The application may discover hardware information dynamically, but the initial design may assume this general system configuration.

---

## 5. Technology Stack

### 5.1 Language

- C++20

C++ is selected because it supports:

- Native Windows integration
- Small deployment size
- Fast startup
- Low runtime overhead
- Direct control over rendering and system APIs
- Single-executable distribution

### 5.2 Windowing and Input

- Native Win32 API

Win32 will be responsible for:

- Creating the application window
- Running the message loop
- Mouse input
- Keyboard input where applicable
- Dragging the borderless window
- Right-click context menus
- Always-on-top behavior
- Window opacity
- Monitor work-area detection
- DPI changes
- Window positioning
- Application shutdown

### 5.3 Graphics and Text

- Direct2D
- DirectWrite

Direct2D will render:

- Component cards
- Utilization graphs
- Graph grids
- Backgrounds
- Selection highlights
- Separators
- Simple icons and geometric elements

DirectWrite will render:

- Component names
- Current utilization values
- Hardware names
- Graph labels
- Detailed statistics
- Contextual text

### 5.4 System Metrics

Initial providers:

- Windows system APIs for CPU information and total CPU utilization
- NVIDIA Management Library, or NVML, for GPU metrics

Later providers:

- Windows memory APIs for physical memory usage
- Windows Performance Data Helper, or PDH, counters for disk utilization and throughput

### 5.5 GPU Integration

NVML should be loaded dynamically at runtime rather than creating a mandatory startup dependency.

The application should:

1. Locate the NVML DLL installed by the NVIDIA driver.
2. Load the DLL dynamically.
3. Resolve only the required NVML functions.
4. Initialize NVML.
5. Discover the target NVIDIA GPU.
6. Query supported metrics.
7. Gracefully disable unavailable metrics.
8. Continue running CPU monitoring if NVML is unavailable.

The NVIDIA driver installation supplies NVML. The application should not distribute its own copy of the NVML DLL.

### 5.6 Build System

- CMake
- Visual Studio 2022 CMake generator
- MSVC compiler and linker

The project will not rely on manually maintained:

- `.sln` files
- `.vcxproj` files
- Visual Studio-specific build configuration

CMake will remain the source of truth for the build.

### 5.7 Development Environment

Recommended development setup:

- Visual Studio Code
- CMake Tools extension
- Microsoft C/C++ extension
- CMake
- Visual Studio Build Tools
- Windows 11 SDK
- Git

The Visual Studio IDE is not required.

---

## 6. Build and Packaging Requirements

### 6.1 Output

The normal release build should produce:

```text
PerformanceMonitor.exe
```

The user should be able to launch the application by double-clicking the executable.

### 6.2 Single-Executable Distribution

The application should:

- Require no installer
- Require no adjacent configuration files
- Require no bundled runtime DLLs
- Require no bundled asset files
- Store small persistent settings in an appropriate user-specific Windows location

System DLLs provided by Windows and NVIDIA are allowed runtime dependencies.

### 6.3 Runtime Linkage

The release build should statically link the MSVC runtime when practical.

Preferred release configuration:

- MSVC `/MT`
- Link-time optimization enabled
- Debug information optionally emitted to a separate PDB
- No console window
- Windows GUI subsystem

### 6.4 Application Resources

The Release 1.0 executable embeds:

- Application icon
- Version information
- DPI-awareness manifest
- Other small native resources

### 6.5 CMake Configurations

At minimum, provide:

- `windows-debug`
- `windows-release`

Recommended generator:

- Visual Studio 17 2022 (x64)

Recommended configuration mechanism:

- `CMakePresets.json`

---

## 7. High-Level User Interface

## 7.1 General Layout

The application has two primary visual regions:

1. **Component rail**
2. **Detail pane**

The component rail is always visible. It contains vertically stacked component cards.

The detail pane is hidden while the application is collapsed. Selecting a component expands the window rightward and displays the corresponding detailed view.

### 7.1.1 Collapsed Mode

The collapsed window contains only the component rail.

Initial component cards:

- CPU
- GPU

Future component cards:

- Memory
- Disk 0
- Disk 1
- Additional components if later approved

Each component card contains:

- Component name
- Current primary utilization value
- Small scrolling history graph
- Optional short hardware label
- Visual indication when selected

### 7.1.2 Expanded Mode

When a component is selected:

- The component rail remains visible.
- The window expands rightward.
- The detail pane displays larger graphs and additional statistics.
- Only one component may be selected at a time.

### 7.1.3 Selection Behavior

- Clicking an unselected component selects it and expands the detail pane.
- Clicking the currently selected component collapses the detail pane.
- Clicking another component while expanded switches the detail pane directly to that component.
- Only one detail pane exists.
- Component switching should not discard monitoring history.

### 7.1.4 Expansion Near Screen Edges

The application should prefer expanding rightward.

If rightward expansion would move part of the window outside the current monitor's work area:

1. Shift the expanded window left enough to remain visible.
2. Preserve the logical anchor position of the collapsed component rail.
3. Restore the collapsed rail to its previous position when the detail pane closes, where practical.

Behavior should account for:

- Multi-monitor systems
- Different monitor resolutions
- Different DPI scaling values
- The Windows taskbar work area

---

## 7.2 Window Style

The main window should be:

- Borderless
- Without a standard title bar
- Without minimize, maximize, and close buttons
- Excluded from the taskbar
- Excluded from Alt+Tab where practical
- Always on top by default
- Draggable by left-clicking and dragging an unused area
- Right-clickable for the application menu
- Fixed-size within each predefined layout mode for the initial version

The user should not need to target a narrow custom title bar to move the window.

---

## 7.3 Approximate Window Sizes

Release 1.0 logical size presets:

| Preset | Collapsed, CPU and GPU | Expanded | Component rail |
|---|---:|---:|---:|
| Small | 175 × 180 | 590 × 420 | 175 wide |
| Medium | 195 × 200 | 660 × 480 | 195 wide |
| Large | 220 × 225 | 760 × 560 | 220 wide |

Medium preserves the tuned Phase 5 layout. If only one component card is visible, the collapsed rail height is reduced to avoid leaving an empty second-card slot.

All layout dimensions should be interpreted in DPI-independent units.

---

## 7.4 Visual Style

The visual style should be:

- Compact
- Modern
- Minimal
- Easy to read at a glance
- Inspired by Task Manager without copying it exactly
- Suitable for remaining visible on the desktop for long periods

Recommended characteristics:

- Dark theme initially
- Subtle card backgrounds
- Thin graph grids
- Clear selected-component state
- Limited decorative elements
- High-contrast current utilization numbers
- Consistent spacing and typography
- No unnecessary animation

A light theme may be considered later.

---

## 7.5 Graph Behavior

Graphs should behave as sideways scrolling history graphs:

- New samples enter from the right.
- Older samples move left.
- The rightmost sample is the latest value.
- Samples are stored independently of rendering.
- The small and large graphs use the same history data.
- Graph history remains available while switching components.

Release 1.0 behavior:

- Sampling interval is fixed at 1 second.
- Displayed history is fixed at 60 seconds.
- Sampling interval and history duration are intentionally not user-configurable.
- Percentage graph range: 0–100%
- Graph redraw: when a new sample arrives or the window requires repainting

The renderer should not use a continuous 60 FPS loop.

### 7.5.1 Graph Elements

A graph may contain:

- Background
- Horizontal and vertical grid lines
- Utilization line
- Optional translucent area fill
- Current value
- Maximum scale label
- Time-range label

Graph styling should prioritize readability over decoration.

### 7.5.2 Smoothing

No smoothing is required for the first version.

An optional moving average or visual smoothing mode may be considered later, but the raw sampled history should remain the underlying source.

---

## 8. CPU Monitoring Specification

## 8.1 Initial CPU Metrics

The CPU provider should initially collect:

- Total CPU utilization percentage
- Processor name
- Physical core count
- Logical processor count
- System uptime

The total utilization percentage is the primary time-series metric.

### 8.1.1 CPU Sampling

CPU utilization should be calculated from differences between cumulative Windows CPU time counters across samples.

The first sample may not produce a utilization value because a previous counter state is required.

The provider should:

- Handle counter wraparound safely
- Clamp invalid values to the valid range
- Avoid producing NaN or infinity
- Mark unavailable samples clearly
- Avoid blocking the UI thread

## 8.2 CPU Component Card

The collapsed CPU card should display:

- `CPU`
- Current total utilization
- Small total-utilization graph
- Optional processor label such as `Ryzen 9 5900X`

The primary display should remain readable at the smallest supported window size.

## 8.3 CPU Detail Pane

The initial CPU detail pane should display:

- Large total-utilization graph
- Current utilization
- Processor name
- Physical core count
- Logical processor count
- System uptime
- Graph scale
- Displayed history duration

Potential later additions:

- Per-logical-processor graphs
- Per-core graphs
- Current or effective frequency
- Process count
- Thread count
- Handle count
- Context-switch rate
- CPU temperature

These later additions are not required for the CPU MVP.

---

## 9. GPU Monitoring Specification

## 9.1 Initial GPU Metrics

The NVIDIA GPU provider should attempt to collect:

- Overall GPU utilization
- Dedicated GPU memory used
- Dedicated GPU memory total
- GPU temperature
- Power usage
- Graphics clock
- Memory clock
- GPU name
- Driver version

Not every metric is guaranteed to be supported.

Each metric should have an availability state.

## 9.2 GPU Component Card

The collapsed GPU card should display:

- `GPU`
- Current overall GPU utilization
- Small GPU-utilization graph
- Optional hardware label such as `RTX 5070`

## 9.3 GPU Detail Pane

The initial GPU detail pane should display:

- Large GPU-utilization graph
- Dedicated GPU-memory usage graph
- Current GPU utilization
- VRAM used and total
- Temperature
- Power usage when supported
- Graphics clock when supported
- Memory clock when supported
- GPU name
- Driver version

Unsupported values should appear as an unavailable marker such as:

```text
—
```

The absence of an optional metric must not be treated as a fatal error.

## 9.4 GPU Engine Breakdown

Separate Task Manager-style engine graphs, such as:

- 3D
- Copy
- Video Decode
- Video Encode
- Compute

are outside the initial NVML-based scope.

They may require Windows GPU performance counters or other Windows graphics telemetry and should be treated as a later research item.

---

## 10. Future Memory Monitoring

Memory monitoring is planned after CPU and GPU are stable.

Potential metrics:

- Physical memory used
- Physical memory available
- Total physical memory
- Used percentage
- Committed memory
- Commit limit
- Cached memory
- Paged pool
- Non-paged pool

The initial memory card would likely use physical-memory utilization percentage as its primary graph.

Memory monitoring should use native Windows memory APIs when possible.

---

## 11. Future Disk Monitoring

Disk monitoring is planned after CPU, GPU, and memory.

Target devices:

- Two physical NVMe SSDs

Potential metrics:

- Active time percentage
- Read throughput
- Write throughput
- Read operations per second
- Write operations per second
- Average response time
- Drive model
- Drive capacity

Each physical drive should receive its own component card.

The application should not permanently assume that Windows `Disk 0` and `Disk 1` always map to the same hardware.

A later disk implementation should associate configuration with stable hardware identity where practical, such as:

- Device path
- Model
- Serial number
- Physical drive identifier

Manual drive assignment may be provided if automatic matching proves unreliable.

---

## 12. Context Menu

Right-clicking the window should open an application context menu.

Initial menu items:

- Always on top
- Opacity
  - 60%
  - 80%
  - 100%
- Component visibility
  - Show CPU
  - Show GPU
- Window size
  - Small
  - Medium
  - Large
- Reset window position
- Start with Windows
- Exit

Future component visibility entries:

- Show Memory
- Show Disk 0
- Show Disk 1

Context-menu actions should update the application immediately where practical.

---

## 13. Settings Persistence

The application should persist small user settings, including:

- Window position
- Selected monitor if relevant
- Always-on-top state
- Opacity
- Component visibility
- Window-size preset
- Startup preference
- Theme if themes are later supported

Release 1.0 stores application settings under the current user's Windows Registry hive. Start-with-Windows uses the current user's standard `Run` registry key. No administrator privileges or configuration file adjacent to the executable are required.

Settings corruption should not prevent startup. Invalid settings should fall back to defaults.

---

## 14. Application Architecture

The application should separate:

1. Platform and lifecycle
2. Metric collection
3. Data storage
4. UI state
5. Rendering
6. Persistent settings

A possible conceptual structure is:

```text
PerformanceMonitor/
├── CMakeLists.txt
├── CMakePresets.json
├── resources/
│   ├── app.rc
│   ├── app.ico
│   └── app.manifest
├── src/
│   ├── app/
│   │   ├── application
│   │   ├── main_window
│   │   └── settings
│   ├── monitoring/
│   │   ├── sampler
│   │   ├── cpu_provider
│   │   └── nvidia_gpu_provider
│   ├── model/
│   │   ├── system_sample
│   │   ├── metric_value
│   │   └── history_buffer
│   └── ui/
│       ├── renderer
│       ├── layout
│       ├── component_card
│       ├── detail_panel
│       └── graph_renderer
└── tests/
    ├── history_buffer_tests
    ├── cpu_utilization_tests
    └── layout_tests
```

Exact filenames and class names are implementation decisions, not specification requirements.

---

## 15. Threading Model

## 15.1 UI Thread

The UI thread should own:

- Win32 message loop
- Window state
- Direct2D resources associated with the window
- Input handling
- Context menu
- Rendering
- DPI changes
- Window movement
- Expansion and collapse state

## 15.2 Sampling Thread

A background sampling thread should:

1. Wait until the next sampling deadline.
2. Query each enabled provider.
3. Construct a complete timestamped sample.
4. Update component history buffers.
5. Notify the UI thread that new data is available.

The sampling thread should not draw directly.

## 15.3 Synchronization

Synchronization should be simple and bounded.

Preferred properties:

- Minimal time holding locks
- No blocking hardware query on the UI thread
- No unbounded queues
- No sampling backlog
- Old missed deadlines are skipped rather than replayed
- Rendering reads a stable snapshot

A circular buffer is preferred for history storage.

---

## 16. Data Model

Each metric should distinguish between:

- Valid numeric value
- Temporarily unavailable
- Unsupported
- Provider error
- Not yet sampled

A component sample should include:

- Timestamp
- Primary utilization value
- Optional secondary values
- Per-value availability information

History capacity should be based on:

- Maximum supported history duration
- Minimum supported sampling interval

For example, supporting 120 seconds at one-second sampling requires at least 120 samples per metric, plus a small margin.

---

## 17. Rendering Architecture

The renderer should use retained application data but immediate drawing commands.

Recommended responsibilities:

### 17.1 Graph Renderer

Reusable for both small and large graphs.

Inputs may include:

- Destination rectangle
- Sample history
- Value range
- Grid settings
- Label settings
- Fill mode
- Line thickness
- Current value
- Availability state

### 17.2 Component Card Renderer

Responsible for:

- Card background
- Component name
- Hardware subtitle
- Current value
- Small graph
- Selected state
- Hover state

### 17.3 Detail Pane Renderer

Responsible for:

- Component title
- Large graphs
- Numeric statistics
- Labels
- Layout of available and unavailable values

### 17.4 Device Resources

Direct2D and DirectWrite resources should be categorized appropriately:

- Device-independent resources
- Device-dependent resources
- Window-size-dependent resources

The application should recreate device resources if the graphics device or render target is lost.

---

## 18. UI State Model

At minimum, UI state should include:

- Collapsed or expanded
- Selected component
- Hovered component
- Current window-size preset
- Current opacity
- Current DPI
- Current monitor work area
- Always-on-top state
- Visible component list

A selected component must always refer to a visible component.

If the selected component is hidden through the context menu, the detail pane should collapse or switch to another valid component using a deterministic rule.

---

## 19. DPI and Multi-Monitor Behavior

The application should be per-monitor DPI aware.

Requirements:

- Layout scales when moved between monitors.
- Text remains sharp.
- Direct2D render targets use the appropriate DPI.
- Window geometry remains inside the monitor work area.
- Saved window positions are validated at startup.
- If the saved monitor is unavailable, the window is moved to a visible default position.
- Expansion logic uses the current monitor's work area.

---

## 20. Error Handling

The application should remain usable when one provider fails.

Examples:

- NVML DLL missing
- NVML initialization failure
- Unsupported GPU metric
- Temporary metric query failure
- Invalid saved settings
- Direct2D device loss
- Unexpected monitor configuration
- First CPU sample unavailable

Expected behavior:

- Continue monitoring supported components.
- Show unavailable values clearly.
- Avoid modal error dialogs during normal runtime.
- Log diagnostic information in debug builds.
- Avoid terminating because an optional metric is unavailable.

A severe initialization error that prevents creating the main window may display a standard error message before exiting.

---

## 21. Performance Targets

These are design targets rather than strict guarantees.

### 21.1 Startup

- Visible window in well under one second on the target desktop
- No command prompt or console window
- No long synchronous hardware scan on the UI thread

### 21.2 Runtime CPU Usage

- Average CPU usage below 1% at a one-second sampling interval on the target system
- No continuous high-frequency render loop
- Repaint primarily when samples or UI state change

### 21.3 Runtime GPU Usage

- Negligible graphics utilization while displaying static or once-per-second updates
- Direct2D rendering should not become a meaningful GPU workload

### 21.4 Memory Usage

- Target working set below 50 MB
- Bounded history buffers
- No ongoing unbounded allocation growth

### 21.5 Executable Size

- Preferred release executable below 10 MB
- A somewhat larger executable is acceptable if required by static runtime linkage and embedded resources

---

## 22. Accuracy Expectations

The application is a trend monitor, not a profiling tool.

Expected accuracy:

- Idle readings should appear reasonable.
- Sustained CPU loads should approach expected utilization.
- GPU-heavy applications should visibly increase GPU load.
- VRAM-heavy applications should visibly increase dedicated-memory usage.
- Trends should broadly agree with Task Manager and `nvidia-smi`.
- Small instantaneous differences from other tools are acceptable.

The application should not claim exact synchronization with Task Manager.

---

## 23. Security and Privilege Model

The initial application should:

- Run as the current user
- Avoid administrator privileges
- Avoid kernel drivers
- Avoid writing to protected system locations
- Avoid downloading or executing external code
- Load only expected system or NVIDIA libraries
- Validate persistent settings before use

No telemetry should be sent outside the machine.

---

## 24. Testing Strategy

## 24.1 Unit Tests

Priority unit-test areas:

- Circular history buffer
- CPU utilization delta calculation
- Value clamping
- Missing-data handling
- Time-range selection
- Layout calculations
- Expansion boundary calculations
- Settings validation
- Formatting of percentages, memory, clocks, power, and uptime

## 24.2 Provider Tests

CPU provider tests:

- First-sample behavior
- Idle system behavior
- Sustained CPU load
- Valid 0–100% range
- Core and logical-processor discovery

GPU provider tests:

- NVML available
- NVML unavailable
- Supported metric
- Unsupported metric
- Temporary query failure
- Driver version and device-name retrieval

## 24.3 Manual UI Tests

- Drag window on one monitor
- Drag window across monitors with different DPI
- Expand near the right edge
- Expand near the left edge
- Collapse after an adjusted expansion position
- Switch directly from CPU to GPU
- Right-click each visible region
- Change opacity
- Toggle always-on-top
- Exit through the context menu
- Restore saved position
- Recover from an off-screen saved position

## 24.4 Validation Against Existing Tools

Compare broad trends with:

- Windows Task Manager
- `nvidia-smi`

The comparison should focus on trends and sustained loads, not exact sample-by-sample equality.

---

## 25. Roadmap

## Phase 0 — Specification and Design

Deliverables:

- Approved project specification
- Initial UI wireframe
- CPU detail-pane content decision
- GPU detail-pane content decision
- Project directory structure
- CMake and toolchain decision
- Initial performance targets

Exit criteria:

- Scope is clear enough to begin implementation.
- Major architectural decisions are recorded.
- No unresolved decision blocks project setup.

## Phase 1 — Project Skeleton

Deliverables:

- CMake project
- CMake presets
- Visual Studio 2022 generator and MSVC build
- Windows GUI executable
- Win32 message loop
- Borderless main window
- Direct2D initialization
- DirectWrite initialization
- Dark background
- Clean launch and shutdown

Exit criteria:

- Debug and Release builds succeed from VS Code and command line.
- The program launches without a console window.
- The window can be moved and closed.

## Phase 2 — Static UI Prototype

Deliverables:

- Component rail
- CPU and GPU cards
- Placeholder small graphs
- Expanded detail pane
- CPU/GPU selection behavior
- Collapse-on-second-click behavior
- Rightward expansion
- Screen-edge adjustment
- DPI-aware layout

Exit criteria:

- UI interactions work using placeholder data.
- No monitoring provider is required yet.
- Layout remains usable on the target monitor configuration.

## Phase 3 — Shared Graph and History Infrastructure

Deliverables:

- Circular history buffer
- Reusable graph renderer
- Timestamped samples
- One-second update timer or sampling loop
- Small and large graph rendering from the same data
- Fixed 60-second displayed history

Exit criteria:

- Simulated data scrolls correctly.
- Graphs retain history while switching components.
- No continuous rendering loop is required.

## Phase 4 — CPU Monitoring MVP

Deliverables:

- CPU provider
- Total CPU utilization
- Processor name
- Core count
- Logical-processor count
- System uptime
- CPU card integration
- CPU detail-pane integration

Exit criteria:

- CPU utilization responds correctly to idle and load tests.
- Values broadly agree with Task Manager.
- UI remains responsive.
- CPU provider failure does not crash the application.

## Phase 5 — NVIDIA GPU Monitoring MVP

Deliverables:

- Dynamic NVML loader
- GPU discovery
- Overall GPU utilization
- VRAM usage
- Temperature
- Power and clocks when supported
- GPU card integration
- GPU detail-pane integration
- Graceful unavailable-state handling

Exit criteria:

- GPU utilization responds to GPU workloads.
- VRAM usage broadly agrees with `nvidia-smi`.
- CPU monitoring still works when NVML is unavailable.

## Phase 6 — Context Menu and Settings

Deliverables:

- Right-click menu
- Always-on-top toggle
- Opacity selection
- Component visibility
- Window-size presets
- Position persistence
- Startup preference
- Reset-position command

Exit criteria:

- Settings survive restart.
- Invalid settings recover to defaults.
- The program remains a single portable executable.

## Phase 7 — Hardening and Release 1.0

**Implementation status:** Release 1.0 source hardening and packaging are complete. Final Windows build/runtime validation is performed on the target machine.

Deliverables:

- Error handling review
- Device-loss handling
- Multi-monitor testing
- DPI testing
- Performance measurements
- Resource cleanup review
- Release build configuration
- Embedded icon and version metadata
- User-facing README
- Initial release executable

Exit criteria:

- Meets core functional requirements.
- CPU and GPU views are stable.
- Runtime overhead is acceptably low.
- No known crash occurs during normal use.

## Phase 8 — Memory Monitoring

Deliverables:

- Memory provider
- Memory card
- Memory detail pane
- Used and total physical memory
- Memory utilization history
- Additional memory statistics as approved

## Phase 9 — Disk Monitoring

Deliverables:

- Physical-disk discovery
- Two NVMe cards
- Active-time history
- Read/write throughput
- Stable device mapping
- Disk detail panes

## Phase 10 — Optional Enhancements

Candidate enhancements:

- Per-core CPU graphs
- CPU frequency
- GPU engine graphs
- Light theme
- Click-through mode
- Tray icon
- Graph smoothing
- Custom colors
- Network monitoring
- Threshold indicators
- Optional data logging
- CPU temperatures through an external sensor backend
- More flexible window resizing

These enhancements require separate approval before entering scope.

---

## 26. MVP Definition

The minimum viable product is:

> A C++20 Windows 11 desktop widget built with CMake, the Visual Studio 2022 generator, MSVC, Win32, Direct2D, and DirectWrite. The collapsed view contains vertically arranged CPU and GPU component cards with one-second scrolling utilization graphs. Selecting a card expands the window rightward to reveal a detailed pane. Selecting the same card again collapses the pane, while selecting another card switches the detail view. CPU monitoring includes total utilization and basic processor information. GPU monitoring uses dynamically loaded NVML and includes total utilization, dedicated-memory usage, temperature, and available supporting statistics. The application is borderless, lightweight, always on top by default, controlled through a right-click menu, and distributed as a single portable executable.

---

## 27. Release 1.0 Acceptance Criteria

Release 1.0 is complete when all of the following are true:

### Build

- Builds with CMake, the Visual Studio 2022 generator, and MSVC.
- Builds from VS Code without opening the Visual Studio IDE.
- Produces one portable executable.
- Release build opens without a console window.

### Window

- Borderless window displays correctly.
- Window can be dragged.
- Window can remain always on top.
- Right-click menu works.
- Window position is restored after restart.
- Window remains visible across monitor and DPI changes.

### Component Rail

- CPU and GPU cards are visible.
- Each card shows current utilization.
- Each card shows a scrolling history graph.
- Selection state is clear.

### Expansion

- Clicking CPU expands CPU details.
- Clicking CPU again collapses the window.
- Clicking GPU while CPU is selected switches to GPU details.
- Expansion remains within the monitor work area.

### CPU

- Total utilization updates at the fixed one-second sampling interval.
- CPU graph displays retained history.
- Processor name, core count, logical-processor count, and uptime display correctly.
- Values broadly track Task Manager.

### GPU

- GPU utilization updates through NVML.
- GPU history is displayed.
- VRAM usage is displayed.
- Temperature is displayed when supported.
- Optional metrics fail gracefully.
- The application still runs when NVML is unavailable.
- Values broadly track `nvidia-smi`.

### Runtime

- UI remains responsive.
- No unbounded memory growth occurs.
- Monitoring and rendering overhead remain low.
- No administrator privileges are required.
- No external telemetry is sent.

---

## 28. Open Design Questions

The following decisions may be finalized during UI prototyping:

1. Exact collapsed and expanded dimensions
2. Exact font family and sizes
3. Whether small graphs use line-only or line-plus-fill rendering
4. Whether the selected card uses a border, background, accent bar, or combination
5. Exact CPU detail-pane statistic placement
6. Exact GPU graph proportions
7. Whether GPU utilization and VRAM graphs are stacked vertically or placed side by side
8. Whether the collapsed card displays the full hardware name
9. Whether expansion and collapse are immediate or use a short animation
10. Registry versus user-configuration-file settings storage
11. Exact behavior of the startup-with-Windows option
12. Whether window-size presets affect both the component rail and detail pane

These questions do not block the project skeleton or initial static UI work.

---

## 29. Decision Log

### Approved Decisions

- Use C++20.
- Use native Win32.
- Use Direct2D and DirectWrite.
- Use CMake rather than manually maintained Visual Studio project files.
- Use the Visual Studio 2022 CMake generator and MSVC.
- Develop primarily through VS Code.
- Produce a single portable executable.
- Start with CPU and GPU.
- Add memory and disks later.
- Use vertically stacked component cards.
- Show a small graph on each component card.
- Expand the selected component rightward.
- Collapse when the selected component is clicked again.
- Allow only one expanded component at a time.
- Keep the component rail visible while expanded.
- Use one-second sampling.
- Use dynamically loaded NVML.
- Treat the application as a trend monitor rather than a profiling tool.
- Exclude CPU temperature from the initial version.
- Avoid a continuous high-frame-rate render loop.
- Keep the sampling interval fixed at one second for Release 1.0.
- Keep displayed history fixed at 60 seconds for Release 1.0.
- Omit sampling-interval and history-duration controls from Phase 6 settings.
- Use the current-user Windows Registry for Release 1.0 settings persistence.
- Use Small, Medium, and Large fixed window-size presets, with Medium preserving the Phase 5 dimensions.
- Release 1.0 uses semantic version 1.0.0 in the CMake project and Windows version resource.
- Release builds use static MSVC runtime linkage and link-time optimization.
- Recover the Direct2D render target after resize/device-loss failures.
- Reposition the component rail onto a connected monitor after display topology changes only when it is no longer visible.

### Deferred Decisions

- CPU per-core graphs
- GPU engine breakdown
- Memory detail metrics
- Disk data source and stable identity mechanism
- Theme customization
- Logging
- Sensor-library integration
- Advanced animations

---

## 30. Change Management

This specification should be treated as the reference document for future design and implementation discussions.

When project scope or behavior changes:

1. Record the decision in the decision log.
2. Update the affected requirements.
3. Update the roadmap if scheduling or phase boundaries change.
4. Update acceptance criteria if release requirements change.
5. Avoid silently changing implementation behavior without updating the specification.

Implementation details may evolve as long as the observable behavior and project constraints in this document remain satisfied.
