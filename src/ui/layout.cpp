#include "layout.hpp"

#include <algorithm>

namespace perfmon::ui {
namespace {

[[nodiscard]] D2D1_RECT_F EmptyRect() noexcept {
    return D2D1::RectF(0.0F, 0.0F, 0.0F, 0.0F);
}

} // namespace

LayoutMetrics GetLayoutMetrics(WindowSizePreset preset) noexcept {
    switch (preset) {
    case WindowSizePreset::Small:
        return LayoutMetrics{
            175.0F,
            180.0F,
            100.0F,
            590.0F,
            420.0F,
            175.0F,
            8.0F,
            72.0F,
            8.0F};
    case WindowSizePreset::Large:
        return LayoutMetrics{
            220.0F,
            225.0F,
            122.0F,
            760.0F,
            560.0F,
            220.0F,
            8.0F,
            95.0F,
            8.0F};
    case WindowSizePreset::Medium:
    default:
        return LayoutMetrics{};
    }
}

float CollapsedHeightDip(
    WindowSizePreset preset,
    bool show_cpu,
    bool show_gpu) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(preset);
    const int visible_count = static_cast<int>(show_cpu) + static_cast<int>(show_gpu);
    return visible_count <= 1
        ? metrics.single_component_collapsed_height
        : metrics.collapsed_height;
}

float WindowWidthDip(WindowSizePreset preset, bool expanded) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(preset);
    return expanded ? metrics.expanded_width : metrics.collapsed_width;
}

float WindowHeightDip(
    WindowSizePreset preset,
    bool expanded,
    bool show_cpu,
    bool show_gpu) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(preset);
    return expanded
        ? metrics.expanded_height
        : CollapsedHeightDip(preset, show_cpu, show_gpu);
}

float ComponentRailWidthDip(WindowSizePreset preset) noexcept {
    return GetLayoutMetrics(preset).component_rail_width;
}

Layout CalculateLayout(D2D1_SIZE_F client_size, const UiState& state) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(state.window_size);

    Layout layout{};
    layout.client = D2D1::RectF(0.0F, 0.0F, client_size.width, client_size.height);

    const float rail_right = std::min(metrics.component_rail_width, client_size.width);
    layout.component_rail = D2D1::RectF(0.0F, 0.0F, rail_right, client_size.height);

    const float card_right = std::max(metrics.outer_margin, rail_right - metrics.outer_margin);
    float next_card_top = metrics.outer_margin;

    if (state.cpu_visible) {
        layout.cpu_card = D2D1::RectF(
            metrics.outer_margin,
            next_card_top,
            card_right,
            next_card_top + metrics.card_height);
        next_card_top += metrics.card_height + metrics.card_gap;
    } else {
        layout.cpu_card = EmptyRect();
    }

    if (state.gpu_visible) {
        layout.gpu_card = D2D1::RectF(
            metrics.outer_margin,
            next_card_top,
            card_right,
            next_card_top + metrics.card_height);
    } else {
        layout.gpu_card = EmptyRect();
    }

    if (state.IsExpanded() && client_size.width > rail_right) {
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
    return rect.right > rect.left && rect.bottom > rect.top &&
           point.x >= rect.left && point.x < rect.right &&
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
