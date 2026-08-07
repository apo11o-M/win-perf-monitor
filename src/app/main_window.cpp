#include "main_window.hpp"

#include "../monitoring/cpu_provider.hpp"
#include "../monitoring/simulated_providers.hpp"
#include "../text_util.hpp"

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

} // namespace

MainWindow::MainWindow(
    HINSTANCE instance,
    ID2D1Factory* d2d_factory,
    IDWriteFactory* dwrite_factory,
    const NvmlProbeResult& nvml_result)
    : instance_(instance),
      renderer_(d2d_factory, dwrite_factory),
      gpu_name_(Utf8ToWide(nvml_result.gpu_name)),
      gpu_status_(Utf8ToWide(nvml_result.status)) {}

MainWindow::~MainWindow() {
    StopSampler();
}

void MainWindow::Create(int show_command) {
    RegisterWindowClass();

    const float system_dpi = static_cast<float>(GetDpiForSystem());
    const float scale = system_dpi / 96.0F;
    const int initial_width = static_cast<int>(std::lround(ui::kCollapsedWidthDip * scale));
    const int initial_height = static_cast<int>(std::lround(ui::kCollapsedHeightDip * scale));

    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        kWindowTitle,
        WS_POPUP,
        100,
        100,
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
    ApplyWindowRect(
        POINT{100, 100},
        ui::kCollapsedWidthDip,
        ui::kCollapsedHeightDip);

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

POINT MainWindow::ComponentRailAnchorPoint(const RECT& window_rect) const noexcept {
    // The component rail is the persistent part of the widget. Use a point near
    // the center of the collapsed rail so monitor ownership does not flip just
    // because the temporary detail pane changes the overall window area.
    const int rail_width = DipToPixels(ui::kComponentRailWidthDip);
    const int collapsed_height = DipToPixels(ui::kCollapsedHeightDip);

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
        ui_state_.IsExpanded());
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
            performance,
            gpu_name_,
            gpu_status_);
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
    if (component == ui::Component::None) {
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
        ui::kCollapsedWidthDip,
        ui::kCollapsedHeightDip);

    expansion_state_ = {};
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ResizeExpandedFromCollapsedRect(const RECT& collapsed_rect) {
    const int width = DipToPixels(ui::kExpandedWidthDip);
    const int height = DipToPixels(ui::kExpandedHeightDip);

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
            HWND_TOPMOST,
            collapsed_rect.left,
            collapsed_rect.top,
            width,
            height,
            SWP_NOACTIVATE);
        return;
    }

    const RECT& work = monitor_info.rcWork;
    const int x = ClampCoordinate(collapsed_rect.left, width, work.left, work.right);
    const int y = ClampCoordinate(collapsed_rect.top, height, work.top, work.bottom);

    SetWindowPos(
        window_,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE);
}

void MainWindow::SetWindowRectAt(
    POINT position,
    float logical_width,
    float logical_height) {
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        position.x,
        position.y,
        DipToPixels(logical_width),
        DipToPixels(logical_height),
        SWP_NOACTIVATE);
}

void MainWindow::DragWindowFromClientArea() {
    RECT before{};
    GetWindowRect(window_, &before);

    ReleaseCapture();
    SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, 0);

    if (!ui_state_.IsExpanded() || !expansion_state_.active) {
        return;
    }

    RECT after{};
    GetWindowRect(window_, &after);
    if (after.left != before.left || after.top != before.top) {
        // Only this user-driven drag path marks the expansion as moved. Automatic
        // SetWindowPos calls used for edge/DPI handling therefore never turn an
        // implementation adjustment into a persistent user position.
        expansion_state_.moved_while_expanded = true;
    }
}

void MainWindow::ApplyWindowRect(
    POINT preferred_position,
    float logical_width,
    float logical_height) {
    const int width = DipToPixels(logical_width);
    const int height = DipToPixels(logical_height);

    const POINT probe_point = preferred_position;
    const HMONITOR monitor = MonitorFromPoint(probe_point, MONITOR_DEFAULTTONEAREST);
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
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE);
}

void MainWindow::HandleDpiChanged(WPARAM w_param, LPARAM l_param) {
    dpi_ = static_cast<float>(HIWORD(w_param));
    renderer_.SetDpi(dpi_);

    const auto* suggested = reinterpret_cast<const RECT*>(l_param);
    const float logical_width =
        ui_state_.IsExpanded() ? ui::kExpandedWidthDip : ui::kCollapsedWidthDip;
    const float logical_height =
        ui_state_.IsExpanded() ? ui::kExpandedHeightDip : ui::kCollapsedHeightDip;

    // Respect Windows' suggested top-left when crossing DPI boundaries, but keep
    // our fixed logical size. Re-running monitor selection/clamping here can make
    // a boundary-straddling window bounce between monitors while it is being
    // expanded, collapsed, or dragged.
    SetWindowPos(
        window_,
        HWND_TOPMOST,
        suggested->left,
        suggested->top,
        DipToPixels(logical_width),
        DipToPixels(logical_height),
        SWP_NOACTIVATE);

    // original_collapsed_rect remains untouched. It is the user's pre-expansion
    // anchor and must survive automatic DPI/edge adjustments.
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

    // Phase 4 uses the real Windows CPU provider. GPU data remains simulated
    // until the NVML metrics provider replaces it in Phase 5.
    sampler_->AddProvider(std::make_unique<monitoring::CpuProvider>());
    sampler_->AddProvider(std::make_unique<monitoring::SimulatedGpuProvider>());
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
    if (menu == nullptr) {
        return;
    }

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

    if (selected == kExitMenuId) {
        DestroyWindow(window_);
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
