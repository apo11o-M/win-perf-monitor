#pragma once

namespace perfmon::ui {

enum class Component {
    None,
    Cpu,
    Gpu,
};

struct UiState {
    Component selected = Component::None;
    Component hovered = Component::None;

    [[nodiscard]] bool IsExpanded() const noexcept {
        return selected != Component::None;
    }
};

} // namespace perfmon::ui
