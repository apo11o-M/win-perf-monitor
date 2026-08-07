#include "layout.hpp"

#include <algorithm>

namespace perfmon::ui {

Layout CalculateLayout(D2D1_SIZE_F client_size, bool expanded) noexcept {
    constexpr float outer_margin = 10.0F;
    constexpr float card_height = 82.0F;
    constexpr float card_gap = 8.0F;

    Layout layout{};
    layout.client = D2D1::RectF(0.0F, 0.0F, client_size.width, client_size.height);

    const float rail_right = std::min(kComponentRailWidthDip, client_size.width);
    layout.component_rail = D2D1::RectF(0.0F, 0.0F, rail_right, client_size.height);

    const float card_right = std::max(outer_margin, rail_right - outer_margin);
    layout.cpu_card = D2D1::RectF(
        outer_margin,
        outer_margin,
        card_right,
        outer_margin + card_height);
    layout.gpu_card = D2D1::RectF(
        outer_margin,
        outer_margin + card_height + card_gap,
        card_right,
        outer_margin + (card_height * 2.0F) + card_gap);

    if (expanded && client_size.width > rail_right) {
        layout.detail_pane = D2D1::RectF(
            rail_right,
            0.0F,
            client_size.width,
            client_size.height);
    } else {
        layout.detail_pane = D2D1::RectF(rail_right, 0.0F, rail_right, client_size.height);
    }

    return layout;
}

bool ContainsPoint(const D2D1_RECT_F& rect, D2D1_POINT_2F point) noexcept {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

Component HitTestComponent(const Layout& layout, D2D1_POINT_2F point) noexcept {
    if (ContainsPoint(layout.cpu_card, point)) {
        return Component::Cpu;
    }
    if (ContainsPoint(layout.gpu_card, point)) {
        return Component::Gpu;
    }
    return Component::None;
}

} // namespace perfmon::ui
