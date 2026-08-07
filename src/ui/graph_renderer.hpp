#pragma once

#include "../model/history_store.hpp"
#include "../win32_headers.hpp"

#include <d2d1.h>

#include <span>

namespace perfmon::ui {

struct GraphStyle {
    ID2D1Brush* background = nullptr;
    ID2D1Brush* grid = nullptr;
    ID2D1Brush* line = nullptr;
    ID2D1Brush* fill = nullptr;
    float line_width = 1.5F;
    int horizontal_divisions = 4;
    int vertical_divisions = 6;
};

void DrawGraph(
    ID2D1RenderTarget* render_target,
    ID2D1Factory* factory,
    const D2D1_RECT_F& bounds,
    std::span<const model::TimestampedMetric> samples,
    model::SampleTime window_start,
    model::SampleTime window_end,
    const GraphStyle& style);

} // namespace perfmon::ui
