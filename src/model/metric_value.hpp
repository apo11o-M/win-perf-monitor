#pragma once

#include <algorithm>

namespace perfmon::model {

enum class MetricState {
    NotYetSampled,
    Valid,
    TemporarilyUnavailable,
    Unsupported,
    ProviderError,
};

struct MetricValue {
    float value = 0.0F;
    MetricState state = MetricState::NotYetSampled;

    [[nodiscard]] static MetricValue Valid(float value) noexcept {
        return MetricValue{value, MetricState::Valid};
    }

    [[nodiscard]] static MetricValue ValidPercentage(float value) noexcept {
        return MetricValue{std::clamp(value, 0.0F, 100.0F), MetricState::Valid};
    }

    [[nodiscard]] static MetricValue Unavailable(
        MetricState unavailable_state = MetricState::TemporarilyUnavailable) noexcept {
        return MetricValue{0.0F, unavailable_state};
    }

    [[nodiscard]] bool HasValue() const noexcept {
        return state == MetricState::Valid;
    }
};

} // namespace perfmon::model
