#pragma once

#include "../win32_headers.hpp"
#include "ui_state.hpp"

#include <d2d1.h>

namespace perfmon::ui {

struct LayoutMetrics {
    float collapsed_width = 195.0F;
    float collapsed_height = 200.0F;
    float single_component_collapsed_height = 110.0F;
    float expanded_width = 660.0F;
    float expanded_height = 480.0F;
    float component_rail_width = 195.0F;
    float outer_margin = 8.0F;
    float card_height = 82.0F;
    float card_gap = 8.0F;
};

struct Layout {
    D2D1_RECT_F client{};
    D2D1_RECT_F component_rail{};
    D2D1_RECT_F cpu_card{};
    D2D1_RECT_F gpu_card{};
    D2D1_RECT_F detail_pane{};
};

[[nodiscard]] LayoutMetrics GetLayoutMetrics(WindowSizePreset preset) noexcept;
[[nodiscard]] float CollapsedHeightDip(
    WindowSizePreset preset,
    bool show_cpu,
    bool show_gpu) noexcept;
[[nodiscard]] float WindowWidthDip(WindowSizePreset preset, bool expanded) noexcept;
[[nodiscard]] float WindowHeightDip(
    WindowSizePreset preset,
    bool expanded,
    bool show_cpu,
    bool show_gpu) noexcept;
[[nodiscard]] float ComponentRailWidthDip(WindowSizePreset preset) noexcept;

[[nodiscard]] Layout CalculateLayout(
    D2D1_SIZE_F client_size,
    const UiState& state) noexcept;
[[nodiscard]] bool ContainsPoint(const D2D1_RECT_F& rect, D2D1_POINT_2F point) noexcept;
[[nodiscard]] Component HitTestComponent(const Layout& layout, D2D1_POINT_2F point) noexcept;

} // namespace perfmon::ui
