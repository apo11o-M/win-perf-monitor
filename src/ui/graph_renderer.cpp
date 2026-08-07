#include "graph_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace perfmon::ui {
namespace {

[[nodiscard]] D2D1_POINT_2F SamplePoint(
    const D2D1_RECT_F& bounds,
    const model::TimestampedMetric& sample,
    model::SampleTime window_start,
    model::SampleTime window_end) noexcept {
    const float width = bounds.right - bounds.left;
    const float height = bounds.bottom - bounds.top;

    const auto window = window_end - window_start;
    const auto offset = sample.timestamp - window_start;
    float normalized_x = 1.0F;
    if (window > model::SampleClock::duration::zero()) {
        normalized_x = static_cast<float>(
            std::chrono::duration<double>(offset).count() /
            std::chrono::duration<double>(window).count());
    }
    normalized_x = std::clamp(normalized_x, 0.0F, 1.0F);

    const float normalized_value =
        std::clamp(sample.metric.value, 0.0F, 100.0F) / 100.0F;

    return D2D1::Point2F(
        bounds.left + (normalized_x * width),
        bounds.bottom - (normalized_value * height));
}

void DrawGrid(
    ID2D1RenderTarget* render_target,
    const D2D1_RECT_F& bounds,
    const GraphStyle& style) {
    if (style.grid == nullptr) {
        return;
    }

    const int horizontal_divisions = std::max(style.horizontal_divisions, 1);
    const int vertical_divisions = std::max(style.vertical_divisions, 1);

    for (int index = 1; index < horizontal_divisions; ++index) {
        const float ratio = static_cast<float>(index) /
                            static_cast<float>(horizontal_divisions);
        const float y = bounds.top + ((bounds.bottom - bounds.top) * ratio);
        render_target->DrawLine(
            D2D1::Point2F(bounds.left, y),
            D2D1::Point2F(bounds.right, y),
            style.grid,
            1.0F);
    }

    for (int index = 1; index < vertical_divisions; ++index) {
        const float ratio = static_cast<float>(index) /
                            static_cast<float>(vertical_divisions);
        const float x = bounds.left + ((bounds.right - bounds.left) * ratio);
        render_target->DrawLine(
            D2D1::Point2F(x, bounds.top),
            D2D1::Point2F(x, bounds.bottom),
            style.grid,
            1.0F);
    }
}

void DrawSegment(
    ID2D1RenderTarget* render_target,
    ID2D1Factory* factory,
    const D2D1_RECT_F& bounds,
    std::span<const D2D1_POINT_2F> points,
    const GraphStyle& style) {
    if (points.size() < 2 || style.line == nullptr) {
        return;
    }

    if (style.fill != nullptr) {
        ID2D1PathGeometry* raw_fill_geometry = nullptr;
        if (SUCCEEDED(factory->CreatePathGeometry(&raw_fill_geometry))) {
            ID2D1GeometrySink* raw_sink = nullptr;
            if (SUCCEEDED(raw_fill_geometry->Open(&raw_sink))) {
                raw_sink->BeginFigure(
                    D2D1::Point2F(points.front().x, bounds.bottom),
                    D2D1_FIGURE_BEGIN_FILLED);
                for (const D2D1_POINT_2F& point : points) {
                    raw_sink->AddLine(point);
                }
                raw_sink->AddLine(D2D1::Point2F(points.back().x, bounds.bottom));
                raw_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(raw_sink->Close())) {
                    render_target->FillGeometry(raw_fill_geometry, style.fill);
                }
                raw_sink->Release();
            }
            raw_fill_geometry->Release();
        }
    }

    ID2D1PathGeometry* raw_line_geometry = nullptr;
    if (FAILED(factory->CreatePathGeometry(&raw_line_geometry))) {
        return;
    }

    ID2D1GeometrySink* raw_sink = nullptr;
    if (SUCCEEDED(raw_line_geometry->Open(&raw_sink))) {
        raw_sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_HOLLOW);
        for (std::size_t index = 1; index < points.size(); ++index) {
            raw_sink->AddLine(points[index]);
        }
        raw_sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if (SUCCEEDED(raw_sink->Close())) {
            render_target->DrawGeometry(
                raw_line_geometry,
                style.line,
                style.line_width);
        }
        raw_sink->Release();
    }

    raw_line_geometry->Release();
}

} // namespace

void DrawGraph(
    ID2D1RenderTarget* render_target,
    ID2D1Factory* factory,
    const D2D1_RECT_F& bounds,
    std::span<const model::TimestampedMetric> samples,
    model::SampleTime window_start,
    model::SampleTime window_end,
    const GraphStyle& style) {
    if (render_target == nullptr || factory == nullptr ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return;
    }

    if (style.background != nullptr) {
        render_target->FillRectangle(bounds, style.background);
    }
    DrawGrid(render_target, bounds, style);

    if (samples.empty() || style.line == nullptr || window_end <= window_start) {
        return;
    }

    // Unavailable samples deliberately break the path instead of connecting
    // across missing provider data. This will matter once real providers are
    // introduced in Phases 4 and 5.
    std::vector<D2D1_POINT_2F> segment;
    segment.reserve(samples.size());

    const auto flush_segment = [&] {
        DrawSegment(render_target, factory, bounds, segment, style);
        segment.clear();
    };

    for (const model::TimestampedMetric& sample : samples) {
        if (!sample.metric.HasValue()) {
            flush_segment();
            continue;
        }

        segment.push_back(SamplePoint(bounds, sample, window_start, window_end));
    }
    flush_segment();
}

} // namespace perfmon::ui
