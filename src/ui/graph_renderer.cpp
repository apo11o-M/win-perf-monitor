#include "graph_renderer.hpp"

#include <algorithm>
#include <vector>

namespace perfmon::ui {
namespace {

[[nodiscard]] D2D1_POINT_2F SamplePoint(
    const D2D1_RECT_F& bounds,
    std::size_t index,
    std::size_t sample_count,
    float value) noexcept {
    const float width = bounds.right - bounds.left;
    const float height = bounds.bottom - bounds.top;
    const float normalized_x = sample_count > 1
        ? static_cast<float>(index) / static_cast<float>(sample_count - 1)
        : 1.0F;
    const float normalized_value = std::clamp(value, 0.0F, 100.0F) / 100.0F;

    return D2D1::Point2F(
        bounds.left + (normalized_x * width),
        bounds.bottom - (normalized_value * height));
}

void DrawGrid(
    ID2D1RenderTarget* render_target,
    const D2D1_RECT_F& bounds,
    ID2D1Brush* grid_brush) {
    if (grid_brush == nullptr) {
        return;
    }

    constexpr int horizontal_divisions = 4;
    constexpr int vertical_divisions = 6;

    for (int index = 1; index < horizontal_divisions; ++index) {
        const float ratio = static_cast<float>(index) /
                            static_cast<float>(horizontal_divisions);
        const float y = bounds.top + ((bounds.bottom - bounds.top) * ratio);
        render_target->DrawLine(
            D2D1::Point2F(bounds.left, y),
            D2D1::Point2F(bounds.right, y),
            grid_brush,
            1.0F);
    }

    for (int index = 1; index < vertical_divisions; ++index) {
        const float ratio = static_cast<float>(index) /
                            static_cast<float>(vertical_divisions);
        const float x = bounds.left + ((bounds.right - bounds.left) * ratio);
        render_target->DrawLine(
            D2D1::Point2F(x, bounds.top),
            D2D1::Point2F(x, bounds.bottom),
            grid_brush,
            1.0F);
    }
}

} // namespace

void DrawGraph(
    ID2D1RenderTarget* render_target,
    ID2D1Factory* factory,
    const D2D1_RECT_F& bounds,
    std::span<const float> values,
    const GraphStyle& style) {
    if (render_target == nullptr || factory == nullptr ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return;
    }

    if (style.background != nullptr) {
        render_target->FillRectangle(bounds, style.background);
    }
    DrawGrid(render_target, bounds, style.grid);

    if (values.size() < 2 || style.line == nullptr) {
        return;
    }

    std::vector<D2D1_POINT_2F> points;
    points.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        points.push_back(SamplePoint(bounds, index, values.size(), values[index]));
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

} // namespace perfmon::ui
