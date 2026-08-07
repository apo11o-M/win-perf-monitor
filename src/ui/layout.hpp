#pragma once

#include "../win32_headers.hpp"
#include "ui_state.hpp"

#include <d2d1.h>

namespace perfmon::ui {

inline constexpr float kCollapsedWidthDip = 220.0F;
inline constexpr float kCollapsedHeightDip = 200.0F;
inline constexpr float kExpandedWidthDip = 760.0F;
inline constexpr float kExpandedHeightDip = 390.0F;
inline constexpr float kComponentRailWidthDip = 220.0F;

struct Layout {
    D2D1_RECT_F client{};
    D2D1_RECT_F component_rail{};
    D2D1_RECT_F cpu_card{};
    D2D1_RECT_F gpu_card{};
    D2D1_RECT_F detail_pane{};
};

[[nodiscard]] Layout CalculateLayout(D2D1_SIZE_F client_size, bool expanded) noexcept;
[[nodiscard]] bool ContainsPoint(const D2D1_RECT_F& rect, D2D1_POINT_2F point) noexcept;
[[nodiscard]] Component HitTestComponent(const Layout& layout, D2D1_POINT_2F point) noexcept;

} // namespace perfmon::ui
