#pragma once

#include "../win32_headers.hpp"

#include "../model/history_store.hpp"
#include "../monitoring/sample_mailbox.hpp"
#include "../monitoring/sampler.hpp"
#include "../ui/layout.hpp"
#include "../ui/renderer.hpp"
#include "../ui/ui_state.hpp"

#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string>

namespace perfmon {

class MainWindow {
public:
    MainWindow(
        HINSTANCE instance,
        ID2D1Factory* d2d_factory,
        IDWriteFactory* dwrite_factory);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Create(int show_command);

private:
    static constexpr UINT_PTR kExitMenuId = 1;
    static constexpr UINT kSampleReadyMessage = WM_APP + 1;

    void RegisterWindowClass() const;
    void Paint();
    void ShowContextMenu(POINT screen_point);
    void UpdateHoverFromClientPoint(POINT client_point);
    void HandleCardClick(ui::Component component);
    void ExpandForComponent(ui::Component component);
    void Collapse();
    void ResizeExpandedFromCollapsedRect(const RECT& collapsed_rect);
    void ApplyWindowRect(POINT preferred_position, float logical_width, float logical_height);
    void SetWindowRectAt(POINT position, float logical_width, float logical_height);
    void DragWindowFromClientArea();
    void HandleDpiChanged(WPARAM w_param, LPARAM l_param);

    void StartSampler();
    void StopSampler() noexcept;
    void HandleSampleReady();

    [[nodiscard]] ui::Layout CurrentLayout() const noexcept;
    [[nodiscard]] D2D1_POINT_2F ClientPixelsToDip(POINT point) const noexcept;
    [[nodiscard]] ui::Component HitTestClientPoint(POINT point) const noexcept;
    [[nodiscard]] float DpiScale() const noexcept;
    [[nodiscard]] int DipToPixels(float value) const noexcept;
    [[nodiscard]] POINT ComponentRailAnchorPoint(const RECT& window_rect) const noexcept;
    [[nodiscard]] HMONITOR ComponentRailMonitor(const RECT& window_rect) const noexcept;

    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    float dpi_ = 96.0F;

    struct ExpansionState {
        bool active = false;
        bool moved_while_expanded = false;
        RECT original_collapsed_rect{};
        HMONITOR anchor_monitor = nullptr;
    };

    bool tracking_mouse_leave_ = false;
    ExpansionState expansion_state_{};

    ui::UiState ui_state_{};
    ui::Renderer renderer_;

    // History is owned and mutated only by the UI thread. The sampling thread
    // communicates through a bounded one-slot mailbox and a WM_APP message.
    model::HistoryStore history_store_{};
    monitoring::SampleMailbox sample_mailbox_{};
    std::unique_ptr<monitoring::Sampler> sampler_{};

};

} // namespace perfmon
