#include "layout.hpp"

#include <algorithm>

namespace perfmon::ui {
namespace {

[[nodiscard]] D2D1_RECT_F EmptyRect() noexcept {
    return D2D1::RectF(0.0F, 0.0F, 0.0F, 0.0F);
}

} // namespace

LayoutMetrics GetLayoutMetrics(WindowSizePreset preset) noexcept {
    LayoutMetrics metrics{};

    switch (preset) {
    case WindowSizePreset::Small:
        metrics.collapsed_width = 175.0F;
        metrics.expanded_width = 570.0F;
        metrics.expanded_height = 420.0F;
        metrics.component_rail_width = 175.0F;
        metrics.padding_left = 5.0F;
        metrics.padding_top = 6.0F;
        metrics.padding_right = 5.0F;
        metrics.padding_bottom = 6.0F;
        metrics.card_height = 72.0F;
        metrics.card_gap = 8.0F;
        return metrics;

    case WindowSizePreset::Large:
        metrics.collapsed_width = 220.0F;
        metrics.expanded_width = 760.0F;
        metrics.expanded_height = 560.0F;
        metrics.component_rail_width = 220.0F;
        metrics.padding_left = 5.0F;
        metrics.padding_top = 6.0F;
        metrics.padding_right = 5.0F;
        metrics.padding_bottom = 6.0F;
        metrics.card_height = 95.0F;
        metrics.card_gap = 8.0F;
        return metrics;

    case WindowSizePreset::Medium:
    default:
        metrics.collapsed_width = 195.0F;
        metrics.expanded_width = 660.0F;
        metrics.expanded_height = 480.0F;
        metrics.component_rail_width = 195.0F;
        metrics.padding_left = 5.0F;
        metrics.padding_top = 6.0F;
        metrics.padding_right = 5.0F;
        metrics.padding_bottom = 6.0F;
        metrics.card_height = 82.0F;
        metrics.card_gap = 8.0F;
        return metrics;
    }
}

float CollapsedHeightDip(
    WindowSizePreset preset,
    bool show_cpu,
    bool show_gpu,
    bool show_memory) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(preset);
    const int visible_count = static_cast<int>(show_cpu) +
        static_cast<int>(show_gpu) + static_cast<int>(show_memory);
    const int gap_count = std::max(0, visible_count - 1);

    return metrics.padding_top +
           (static_cast<float>(visible_count) * metrics.card_height) +
           (static_cast<float>(gap_count) * metrics.card_gap) +
           metrics.padding_bottom;
}

float WindowWidthDip(WindowSizePreset preset, bool expanded) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(preset);
    return expanded ? metrics.expanded_width : metrics.collapsed_width;
}

float WindowHeightDip(
    WindowSizePreset preset,
    bool expanded,
    bool show_cpu,
    bool show_gpu,
    bool show_memory) noexcept {
    const LayoutMetrics metrics = GetLayoutMetrics(preset);
    return expanded
        ? metrics.expanded_height
        : CollapsedHeightDip(preset, show_cpu, show_gpu, show_memory);
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

    const float card_right = std::max(
        metrics.padding_left,
        rail_right - metrics.padding_right);
    float next_card_top = metrics.padding_top;

    if (state.cpu_visible) {
        layout.cpu_card = D2D1::RectF(
            metrics.padding_left,
            next_card_top,
            card_right,
            next_card_top + metrics.card_height);
        next_card_top += metrics.card_height + metrics.card_gap;
    } else {
        layout.cpu_card = EmptyRect();
    }

    if (state.gpu_visible) {
        layout.gpu_card = D2D1::RectF(
            metrics.padding_left,
            next_card_top,
            card_right,
            next_card_top + metrics.card_height);
        next_card_top += metrics.card_height + metrics.card_gap;
    } else {
        layout.gpu_card = EmptyRect();
    }

    if (state.memory_visible) {
        layout.memory_card = D2D1::RectF(
            metrics.padding_left,
            next_card_top,
            card_right,
            next_card_top + metrics.card_height);
    } else {
        layout.memory_card = EmptyRect();
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
    if (ContainsPoint(layout.memory_card, point)) {
        return Component::Memory;
    }
    return Component::None;
}

} // namespace perfmon::ui
