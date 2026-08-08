#include "main_window.hpp"

#include "../monitoring/cpu_provider.hpp"
#include "../monitoring/nvidia_gpu_provider.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace perfmon {
namespace {

constexpr wchar_t kWindowClassName[] = L"PerformanceMonitorWindowClass";
constexpr wchar_t kWindowTitle[] = L"Performance Monitor";

[[nodiscard]] int ClampCoordinate(int preferred, int extent, int minimum, int maximum) noexcept {
    const int available = maximum - minimum;
    if (extent >= available) {
        return minimum;
    }
    return std::clamp(preferred, minimum, maximum - extent);
}

[[nodiscard]] UINT CheckedMenuFlags(bool checked) noexcept {
    return MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED);
}

[[nodiscard]] UINT VisibilityMenuFlags(bool checked, bool enabled) noexcept {
    UINT flags = CheckedMenuFlags(checked);
    if (!enabled) {
        flags |= MF_GRAYED;
    }
    return flags;
}

} // namespace

MainWindow::MainWindow(
    HINSTANCE instance,
    ID2D1Factory* d2d_factory,
    IDWriteFactory* dwrite_factory)
    : instance_(instance),
      renderer_(d2d_factory, dwrite_factory) {}

MainWindow::~MainWindow() {
    StopSampler();
}

void MainWindow::Create(int show_command) {
    RegisterWindowClass();

    settings_ = settings_store_.Load();
    ui_state_.cpu_visible = settings_.show_cpu;
    ui_state_.gpu_visible = settings_.show_gpu;
    ui_state_.window_size = settings_.window_size;

    // Keep the Run-key entry synchronized with the persisted preference. When
    // enabled, this also refreshes the command if the portable executable moved.
    settings_store_.SetStartWithWindows(settings_.start_with_windows);

    const POINT initial_position = settings_.has_window_position
        ? settings_.window_position
        : POINT{AppSettings::kDefaultWindowX, AppSettings::kDefaultWindowY};

    const float system_dpi = static_cast<float>(GetDpiForSystem());
    const float scale = system_dpi / 96.0F;
    const int initial_width = static_cast<int>(std::lround(CurrentWindowWidthDip() * scale));
    const int initial_height = static_cast<int>(std::lround(CurrentWindowHeightDip() * scale));

    DWORD extended_style = WS_EX_TOOLWINDOW;
    if (settings_.always_on_top) {
        extended_style |= WS_EX_TOPMOST;
    }
    if (settings_.opacity_percent < 100) {
        extended_style |= WS_EX_LAYERED;
    }

    window_ = CreateWindowExW(
        extended_style,
        kWindowClassName,
        kWindowTitle,
        WS_POPUP,
        initial_position.x,
        initial_position.y,
        initial_width,
        initial_height,
        nullptr,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr) {
        throw std::runtime_error("CreateWindowExW failed");
    }

    dpi_ = static_cast<float>(GetDpiForWindow(window_));

    const int restored_width = DipToPixels(CurrentWindowWidthDip());
    const int restored_height = DipToPixels(CurrentWindowHeightDip());
    const RECT restored_rect{
        initial_position.x,
        initial_position.y,
        initial_position.x + restored_width,
        initial_position.y + restored_height};

    if (settings_.has_window_position &&
        MonitorFromRect(&restored_rect, MONITOR_DEFAULTTONULL) != nullptr) {
        // Preserve an intentionally boundary-straddling position if the saved
        // rail is still visible on at least one connected monitor.
        SetWindowRectAt(
            initial_position,
            CurrentWindowWidthDip(),
            CurrentWindowHeightDip());
    } else {
        // A missing/off-screen saved monitor falls back to the nearest valid
        // work area, which also handles first launch.
        ApplyWindowRect(
            initial_position,
            CurrentWindowWidthDip(),
            CurrentWindowHeightDip());
    }

    ApplyOpacity();
    PersistCurrentWindowPosition();

    ShowWindow(window_, show_command);
    UpdateWindow(window_);
    StartSampler();
}

void MainWindow::RegisterWindowClass() const {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &MainWindow::WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&window_class) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("RegisterClassExW failed");
        }
    }
}

float MainWindow::DpiScale() const noexcept {
    return dpi_ / 96.0F;
}

int MainWindow::DipToPixels(float value) const noexcept {
    return static_cast<int>(std::lround(value * DpiScale()));
}

float MainWindow::CurrentWindowWidthDip() const noexcept {
    return ui::WindowWidthDip(ui_state_.window_size, ui_state_.IsExpanded());
}

float MainWindow::CurrentWindowHeightDip() const noexcept {
    return ui::WindowHeightDip(
        ui_state_.window_size,
        ui_state_.IsExpanded(),
        ui_state_.cpu_visible,
        ui_state_.gpu_visible);
}

POINT MainWindow::PersistentWindowPosition() const noexcept {
    if (window_ == nullptr) {
        return settings_.window_position;
    }

    if (expansion_state_.active && !expansion_state_.moved_while_expanded) {
        return POINT{
            expansion_state_.original_collapsed_rect.left,
            expansion_state_.original_collapsed_rect.top};
    }

    RECT current{};
    if (GetWindowRect(window_, &current) == FALSE) {
        return settings_.window_position;
    }
    return POINT{current.left, current.top};
}

POINT MainWindow::ComponentRailAnchorPoint(const RECT& window_rect) const noexcept {
    // The component rail is the persistent part of the widget. Use a point near
    // the center of the collapsed rail so monitor ownership does not flip just
    // because the temporary detail pane changes the overall window area.
    const int rail_width = DipToPixels(ui::ComponentRailWidthDip(ui_state_.window_size));
    const int collapsed_height = DipToPixels(ui::CollapsedHeightDip(
        ui_state_.window_size,
        ui_state_.cpu_visible,
        ui_state_.gpu_visible));

    return POINT{
        window_rect.left + (rail_width / 2),
        window_rect.top + (collapsed_height / 2)};
}

HMONITOR MainWindow::ComponentRailMonitor(const RECT& window_rect) const noexcept {
    return MonitorFromPoint(
        ComponentRailAnchorPoint(window_rect),
        MONITOR_DEFAULTTONEAREST);
}

D2D1_POINT_2F MainWindow::ClientPixelsToDip(POINT point) const noexcept {
    const float scale = DpiScale();
    return D2D1::Point2F(
        static_cast<float>(point.x) / scale,
        static_cast<float>(point.y) / scale);
}

ui::Layout MainWindow::CurrentLayout() const noexcept {
    RECT client{};
    GetClientRect(window_, &client);
    const float scale = DpiScale();
    return ui::CalculateLayout(
        D2D1::SizeF(
            static_cast<float>(client.right - client.left) / scale,
            static_cast<float>(client.bottom - client.top) / scale),
        ui_state_);
}

ui::Component MainWindow::HitTestClientPoint(POINT point) const noexcept {
    return ui::HitTestComponent(CurrentLayout(), ClientPixelsToDip(point));
}

void MainWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);

    try {
        const model::PerformanceSnapshot performance = history_store_.Snapshot();
        renderer_.Draw(
            window_,
            dpi_,
            ui_state_,
            performance);
    } catch (...) {
        renderer_.DiscardDeviceResources();
    }

    EndPaint(window_, &paint);
}

void MainWindow::UpdateHoverFromClientPoint(POINT client_point) {
    if (!tracking_mouse_leave_) {
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window_;
        if (TrackMouseEvent(&tracking) != FALSE) {
            tracking_mouse_leave_ = true;
        }
    }

    const ui::Component hovered = HitTestClientPoint(client_point);
    if (hovered != ui_state_.hovered) {
        ui_state_.hovered = hovered;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void MainWindow::HandleCardClick(ui::Component component) {
    if (component == ui::Component::None || !ui_state_.IsVisible(component)) {
        return;
    }

    if (ui_state_.selected == component) {
        Collapse();
    } else if (ui_state_.selected == ui::Component::None) {
        ExpandForComponent(component);
    } else {
        ui_state_.selected = component;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void MainWindow::ExpandForComponent(ui::Component component) {
    RECT current{};
    GetWindowRect(window_, &current);

    expansion_state_.active = true;
    expansion_state_.moved_while_expanded = false;
    expansion_state_.original_collapsed_rect = current;
    expansion_state_.anchor_monitor = ComponentRailMonitor(current);

    ui_state_.selected = component;
    ResizeExpandedFromCollapsedRect(current);
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::Collapse() {
    RECT current{};
    GetWindowRect(window_, &current);

    POINT collapse_position{current.left, current.top};
    if (expansion_state_.active && !expansion_state_.moved_while_expanded) {
        // If expansion had to move the window to keep the detail pane on-screen,
        // undo only that automatic movement. The untouched collapsed widget goes
        // back exactly where the user originally left it.
        collapse_position = POINT{
            expansion_state_.original_collapsed_rect.left,
            expansion_state_.original_collapsed_rect.top};
    }

    ui_state_.selected = ui::Component::None;

    // Do not clamp collapse to a single monitor. If the user moved the expanded
    // window, collapse where the rail currently is. Otherwise restore the exact
    // pre-expansion rail location, including intentional boundary-straddling
    // positions.
    SetWindowRectAt(
        collapse_position,
        CurrentWindowWidthDip(),
        CurrentWindowHeightDip());

    expansion_state_ = {};
    PersistCurrentWindowPosition();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ResizeExpandedFromCollapsedRect(const RECT& collapsed_rect) {
    const int width = DipToPixels(ui::WindowWidthDip(ui_state_.window_size, true));
    const int height = DipToPixels(ui::WindowHeightDip(
        ui_state_.window_size,
        true,
        ui_state_.cpu_visible,
        ui_state_.gpu_visible));

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    // Keep the same monitor for the whole expansion operation. In particular,
    // do not ask Windows to choose again from the larger expanded rectangle: a
    // window straddling two monitors can otherwise flip ownership as its area
    // changes, which looks like the widget jumping between displays.
    HMONITOR monitor = expansion_state_.anchor_monitor;
    if (monitor == nullptr) {
        monitor = ComponentRailMonitor(collapsed_rect);
    }

    if (GetMonitorInfoW(monitor, &monitor_info) == FALSE) {
        SetWindowPos(
            window_,
            nullptr,
            collapsed_rect.left,
            collapsed_rect.top,
            width,
            height,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return;
    }

    const RECT& work = monitor_info.rcWork;
    const int x = ClampCoordinate(collapsed_rect.left, width, work.left, work.right);
    const int y = ClampCoordinate(collapsed_rect.top, height, work.top, work.bottom);

    SetWindowPos(
        window_,
        nullptr,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void MainWindow::SetWindowRectAt(
    POINT position,
    float logical_width,
    float logical_height) {
    SetWindowPos(
        window_,
        nullptr,
        position.x,
        position.y,
        DipToPixels(logical_width),
        DipToPixels(logical_height),
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void MainWindow::DragWindowFromClientArea() {
    RECT before{};
    GetWindowRect(window_, &before);

    ReleaseCapture();
    SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, 0);

    RECT after{};
    GetWindowRect(window_, &after);
    if (after.left == before.left && after.top == before.top) {
        return;
    }

    if (ui_state_.IsExpanded() && expansion_state_.active) {
        // Only this user-driven drag path marks the expansion as moved. Automatic
        // SetWindowPos calls used for edge/DPI handling therefore never turn an
        // implementation adjustment into a persistent user position.
        expansion_state_.moved_while_expanded = true;
        expansion_state_.anchor_monitor = ComponentRailMonitor(after);
    }

    PersistCurrentWindowPosition();
}

void MainWindow::ApplyWindowRect(
    POINT preferred_position,
    float logical_width,
    float logical_height) {
    const int width = DipToPixels(logical_width);
    const int height = DipToPixels(logical_height);

    const HMONITOR monitor = MonitorFromPoint(preferred_position, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    int x = preferred_position.x;
    int y = preferred_position.y;
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        const RECT& work = monitor_info.rcWork;
        x = ClampCoordinate(x, width, work.left, work.right);
        y = ClampCoordinate(y, height, work.top, work.bottom);
    }

    SetWindowPos(
        window_,
        nullptr,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void MainWindow::HandleDpiChanged(WPARAM w_param, LPARAM l_param) {
    dpi_ = static_cast<float>(HIWORD(w_param));
    renderer_.SetDpi(dpi_);

    const auto* suggested = reinterpret_cast<const RECT*>(l_param);

    // Respect Windows' suggested top-left when crossing DPI boundaries, but keep
    // our fixed logical size. Re-running monitor selection/clamping here can make
    // a boundary-straddling window bounce between monitors while it is being
    // expanded, collapsed, or dragged.
    SetWindowPos(
        window_,
        nullptr,
        suggested->left,
        suggested->top,
        DipToPixels(CurrentWindowWidthDip()),
        DipToPixels(CurrentWindowHeightDip()),
        SWP_NOACTIVATE | SWP_NOZORDER);

    // original_collapsed_rect remains untouched. It is the user's pre-expansion
    // anchor and must survive automatic DPI/edge adjustments.
}

void MainWindow::ApplyAlwaysOnTop() {
    if (window_ == nullptr) {
        return;
    }

    SetWindowPos(
        window_,
        settings_.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void MainWindow::ApplyOpacity() {
    if (window_ == nullptr) {
        return;
    }

    LONG_PTR extended_style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (settings_.opacity_percent < 100) {
        extended_style |= WS_EX_LAYERED;
    } else {
        extended_style &= ~static_cast<LONG_PTR>(WS_EX_LAYERED);
    }
    SetWindowLongPtrW(window_, GWL_EXSTYLE, extended_style);

    if (settings_.opacity_percent < 100) {
        const BYTE alpha = static_cast<BYTE>(
            (255 * std::clamp(settings_.opacity_percent, 0, 100)) / 100);
        (void)SetLayeredWindowAttributes(window_, 0, alpha, LWA_ALPHA);
    }

    SetWindowPos(
        window_,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ToggleComponentVisibility(ui::Component component) {
    if (component == ui::Component::None) {
        return;
    }

    const bool currently_visible = ui_state_.IsVisible(component);
    if (currently_visible && ui_state_.VisibleComponentCount() <= 1) {
        return;
    }

    if (component == ui::Component::Cpu) {
        ui_state_.cpu_visible = !ui_state_.cpu_visible;
        settings_.show_cpu = ui_state_.cpu_visible;
    } else if (component == ui::Component::Gpu) {
        ui_state_.gpu_visible = !ui_state_.gpu_visible;
        settings_.show_gpu = ui_state_.gpu_visible;
    }

    if (!ui_state_.IsVisible(ui_state_.hovered)) {
        ui_state_.hovered = ui::Component::None;
    }

    if (!ui_state_.IsVisible(ui_state_.selected)) {
        if (ui_state_.cpu_visible) {
            ui_state_.selected = ui::Component::Cpu;
        } else if (ui_state_.gpu_visible) {
            ui_state_.selected = ui::Component::Gpu;
        } else {
            ui_state_.selected = ui::Component::None;
        }
    }

    if (!ui_state_.IsExpanded()) {
        RECT current{};
        GetWindowRect(window_, &current);
        ApplyWindowRect(
            POINT{current.left, current.top},
            CurrentWindowWidthDip(),
            CurrentWindowHeightDip());
        PersistCurrentWindowPosition();
    }

    SaveSettings();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::SetWindowSizePreset(ui::WindowSizePreset preset) {
    if (ui_state_.window_size == preset) {
        return;
    }

    const POINT anchor = PersistentWindowPosition();
    ui_state_.window_size = preset;
    settings_.window_size = preset;

    if (ui_state_.IsExpanded()) {
        expansion_state_.original_collapsed_rect.left = anchor.x;
        expansion_state_.original_collapsed_rect.top = anchor.y;
        expansion_state_.original_collapsed_rect.right =
            anchor.x + DipToPixels(ui::WindowWidthDip(preset, false));
        expansion_state_.original_collapsed_rect.bottom =
            anchor.y + DipToPixels(ui::WindowHeightDip(
                preset,
                false,
                ui_state_.cpu_visible,
                ui_state_.gpu_visible));
        expansion_state_.anchor_monitor = ComponentRailMonitor(expansion_state_.original_collapsed_rect);
        ResizeExpandedFromCollapsedRect(expansion_state_.original_collapsed_rect);
    } else {
        ApplyWindowRect(anchor, CurrentWindowWidthDip(), CurrentWindowHeightDip());
        PersistCurrentWindowPosition();
    }

    SaveSettings();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ResetWindowPosition() {
    HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    POINT reset_position{AppSettings::kDefaultWindowX, AppSettings::kDefaultWindowY};
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        reset_position.x = monitor_info.rcWork.left + AppSettings::kDefaultWindowX;
        reset_position.y = monitor_info.rcWork.top + AppSettings::kDefaultWindowY;
    }

    if (ui_state_.IsExpanded()) {
        expansion_state_.active = true;
        expansion_state_.moved_while_expanded = false;
        expansion_state_.original_collapsed_rect = RECT{
            reset_position.x,
            reset_position.y,
            reset_position.x + DipToPixels(ui::WindowWidthDip(ui_state_.window_size, false)),
            reset_position.y + DipToPixels(ui::WindowHeightDip(
                ui_state_.window_size,
                false,
                ui_state_.cpu_visible,
                ui_state_.gpu_visible))};
        expansion_state_.anchor_monitor = monitor;
        ResizeExpandedFromCollapsedRect(expansion_state_.original_collapsed_rect);
    } else {
        ApplyWindowRect(reset_position, CurrentWindowWidthDip(), CurrentWindowHeightDip());
    }

    PersistCurrentWindowPosition();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ToggleStartWithWindows() {
    settings_.start_with_windows = !settings_.start_with_windows;
    settings_store_.SetStartWithWindows(settings_.start_with_windows);
    SaveSettings();
}

void MainWindow::SaveSettings() noexcept {
    settings_store_.Save(settings_);
}

void MainWindow::PersistCurrentWindowPosition() noexcept {
    if (window_ == nullptr) {
        return;
    }

    settings_.window_position = PersistentWindowPosition();
    settings_.has_window_position = true;
    SaveSettings();
}

void MainWindow::StartSampler() {
    if (sampler_) {
        return;
    }

    sampler_ = std::make_unique<monitoring::Sampler>(
        history_store_.Settings().sampling_interval,
        [this](model::SystemSample sample) {
            if (sample_mailbox_.Publish(std::move(sample))) {
                (void)PostMessageW(window_, kSampleReadyMessage, 0, 0);
            }
        });

    sampler_->AddProvider(std::make_unique<monitoring::CpuProvider>());
    sampler_->AddProvider(std::make_unique<monitoring::NvidiaGpuProvider>());
    sampler_->Start();
}

void MainWindow::StopSampler() noexcept {
    if (sampler_) {
        sampler_->Stop();
        sampler_.reset();
    }
}

void MainWindow::HandleSampleReady() {
    std::optional<model::SystemSample> sample = sample_mailbox_.ConsumeLatest();
    if (!sample.has_value()) {
        return;
    }

    history_store_.Push(*sample);
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ShowContextMenu(POINT screen_point) {
    HMENU menu = CreatePopupMenu();
    HMENU opacity_menu = CreatePopupMenu();
    HMENU visibility_menu = CreatePopupMenu();
    HMENU size_menu = CreatePopupMenu();
    if (menu == nullptr || opacity_menu == nullptr || visibility_menu == nullptr || size_menu == nullptr) {
        if (menu != nullptr) {
            DestroyMenu(menu);
        }
        if (opacity_menu != nullptr) {
            DestroyMenu(opacity_menu);
        }
        if (visibility_menu != nullptr) {
            DestroyMenu(visibility_menu);
        }
        if (size_menu != nullptr) {
            DestroyMenu(size_menu);
        }
        return;
    }

    AppendMenuW(
        menu,
        CheckedMenuFlags(settings_.always_on_top),
        kAlwaysOnTopMenuId,
        L"Always on top");

    AppendMenuW(opacity_menu, CheckedMenuFlags(settings_.opacity_percent == 60), kOpacity60MenuId, L"60%");
    AppendMenuW(opacity_menu, CheckedMenuFlags(settings_.opacity_percent == 80), kOpacity80MenuId, L"80%");
    AppendMenuW(opacity_menu, CheckedMenuFlags(settings_.opacity_percent == 100), kOpacity100MenuId, L"100%");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opacity_menu), L"Opacity");

    const std::size_t visible_count = ui_state_.VisibleComponentCount();
    AppendMenuW(
        visibility_menu,
        VisibilityMenuFlags(ui_state_.cpu_visible, !ui_state_.cpu_visible || visible_count > 1),
        kShowCpuMenuId,
        L"Show CPU");
    AppendMenuW(
        visibility_menu,
        VisibilityMenuFlags(ui_state_.gpu_visible, !ui_state_.gpu_visible || visible_count > 1),
        kShowGpuMenuId,
        L"Show GPU");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(visibility_menu), L"Components");

    AppendMenuW(size_menu, CheckedMenuFlags(ui_state_.window_size == ui::WindowSizePreset::Small), kWindowSmallMenuId, L"Small");
    AppendMenuW(size_menu, CheckedMenuFlags(ui_state_.window_size == ui::WindowSizePreset::Medium), kWindowMediumMenuId, L"Medium");
    AppendMenuW(size_menu, CheckedMenuFlags(ui_state_.window_size == ui::WindowSizePreset::Large), kWindowLargeMenuId, L"Large");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(size_menu), L"Window size");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kResetPositionMenuId, L"Reset window position");
    AppendMenuW(
        menu,
        CheckedMenuFlags(settings_.start_with_windows),
        kStartWithWindowsMenuId,
        L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitMenuId, L"Exit");

    SetForegroundWindow(window_);
    const UINT selected = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        screen_point.x,
        screen_point.y,
        0,
        window_,
        nullptr);
    DestroyMenu(menu);

    if (selected != 0) {
        HandleContextMenuCommand(selected);
    }
}

void MainWindow::HandleContextMenuCommand(UINT command) {
    switch (command) {
    case kAlwaysOnTopMenuId:
        settings_.always_on_top = !settings_.always_on_top;
        ApplyAlwaysOnTop();
        SaveSettings();
        break;

    case kOpacity60MenuId:
    case kOpacity80MenuId:
    case kOpacity100MenuId:
        settings_.opacity_percent = command == kOpacity60MenuId
            ? 60
            : (command == kOpacity80MenuId ? 80 : 100);
        ApplyOpacity();
        SaveSettings();
        break;

    case kShowCpuMenuId:
        ToggleComponentVisibility(ui::Component::Cpu);
        break;

    case kShowGpuMenuId:
        ToggleComponentVisibility(ui::Component::Gpu);
        break;

    case kWindowSmallMenuId:
        SetWindowSizePreset(ui::WindowSizePreset::Small);
        break;

    case kWindowMediumMenuId:
        SetWindowSizePreset(ui::WindowSizePreset::Medium);
        break;

    case kWindowLargeMenuId:
        SetWindowSizePreset(ui::WindowSizePreset::Large);
        break;

    case kResetPositionMenuId:
        ResetWindowPosition();
        break;

    case kStartWithWindowsMenuId:
        ToggleStartWithWindows();
        break;

    case kExitMenuId:
        DestroyWindow(window_);
        break;

    default:
        break;
    }
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case kSampleReadyMessage:
        HandleSampleReady();
        return 0;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_SIZE:
        renderer_.Resize(LOWORD(l_param), HIWORD(l_param));
        return 0;

    case WM_DPICHANGED:
        HandleDpiChanged(w_param, l_param);
        return 0;

    case WM_EXITSIZEMOVE:
        PersistCurrentWindowPosition();
        return 0;

    case WM_MOUSEMOVE:
        UpdateHoverFromClientPoint(
            POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;

    case WM_MOUSELEAVE:
        tracking_mouse_leave_ = false;
        if (ui_state_.hovered != ui::Component::None) {
            ui_state_.hovered = ui::Component::None;
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        const ui::Component component = HitTestClientPoint(point);
        if (component != ui::Component::None) {
            HandleCardClick(component);
        } else {
            DragWindowFromClientArea();
        }
        return 0;
    }

    case WM_SETCURSOR: {
        POINT screen_point{};
        if (GetCursorPos(&screen_point) != FALSE) {
            POINT client_point = screen_point;
            ScreenToClient(window_, &client_point);
            if (HitTestClientPoint(client_point) != ui::Component::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetWindowRect(window_, &rect);
            point = POINT{rect.left + 20, rect.top + 20};
        }
        ShowContextMenu(point);
        return 0;
    }

    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            DestroyWindow(window_);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;

    case WM_DESTROY:
        PersistCurrentWindowPosition();
        StopSampler();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, w_param, l_param);
}

LRESULT CALLBACK MainWindow::WindowProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
    MainWindow* main_window = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        main_window = static_cast<MainWindow*>(create->lpCreateParams);
        main_window->window_ = window;
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(main_window));
    } else {
        main_window = reinterpret_cast<MainWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (main_window != nullptr) {
        return main_window->HandleMessage(message, w_param, l_param);
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace perfmon
