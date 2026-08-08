#pragma once

#include <cstddef>

namespace perfmon::ui {

enum class Component {
    None,
    Cpu,
    Gpu,
};

enum class WindowSizePreset {
    Small = 0,
    Medium = 1,
    Large = 2,
};

struct UiState {
    Component selected = Component::None;
    Component hovered = Component::None;
    bool cpu_visible = true;
    bool gpu_visible = true;
    WindowSizePreset window_size = WindowSizePreset::Medium;

    [[nodiscard]] bool IsExpanded() const noexcept {
        return selected != Component::None;
    }

    [[nodiscard]] bool IsVisible(Component component) const noexcept {
        switch (component) {
        case Component::Cpu:
            return cpu_visible;
        case Component::Gpu:
            return gpu_visible;
        case Component::None:
        default:
            return false;
        }
    }

    [[nodiscard]] std::size_t VisibleComponentCount() const noexcept {
        return static_cast<std::size_t>(cpu_visible) +
               static_cast<std::size_t>(gpu_visible);
    }
};

} // namespace perfmon::ui
