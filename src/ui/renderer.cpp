#include "renderer.hpp"

#include "graph_renderer.hpp"

#include <chrono>
#include <cstddef>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace perfmon::ui {
namespace {

void ThrowIfFailed(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(operation);
    }
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateTextFormat(
    IDWriteFactory* factory,
    float size,
    DWRITE_FONT_WEIGHT weight) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    ThrowIfFailed(
        factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"en-us",
            format.GetAddressOf()),
        "CreateTextFormat failed");
    ThrowIfFailed(
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
        "SetWordWrapping failed");
    return format;
}

D2D1_ROUNDED_RECT Rounded(const D2D1_RECT_F& rect, float radius = 8.0F) noexcept {
    return D2D1_ROUNDED_RECT{rect, radius, radius};
}

std::wstring FormatPercentage(const model::MetricValue& metric) {
    if (!metric.HasValue()) {
        return L"—";
    }
    return std::to_wstring(static_cast<long long>(std::lround(metric.value))) + L"%";
}

std::wstring FormatCount(const std::optional<std::uint32_t>& value) {
    if (!value.has_value()) {
        return L"—";
    }

    std::wstring digits = std::to_wstring(*value);
    for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(digits.size()) - 3;
         index > 0;
         index -= 3) {
        digits.insert(static_cast<std::size_t>(index), 1, L',');
    }
    return digits;
}

std::wstring FormatGigahertz(const model::MetricValue& metric) {
    if (!metric.HasValue()) {
        return L"—";
    }

    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << metric.value << L" GHz";
    return stream.str();
}

std::wstring FormatUptime(const std::optional<std::chrono::milliseconds>& uptime) {
    if (!uptime.has_value()) {
        return L"—";
    }

    const auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(*uptime).count();
    const auto days = total_seconds / 86400;
    const auto hours = (total_seconds / 3600) % 24;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;

    std::wostringstream stream;
    if (days > 0) {
        stream << days << L"d ";
    }
    stream << std::setfill(L'0')
           << std::setw(2) << hours << L":"
           << std::setw(2) << minutes << L":"
           << std::setw(2) << seconds;
    return stream.str();
}

std::wstring CpuDisplayName(const model::PerformanceSnapshot& performance) {
    return performance.cpu_info.processor_name.empty()
        ? L"Processor information unavailable"
        : performance.cpu_info.processor_name;
}

} // namespace

Renderer::Renderer(ID2D1Factory* d2d_factory, IDWriteFactory* dwrite_factory)
    : d2d_factory_(d2d_factory), dwrite_factory_(dwrite_factory) {
    if (d2d_factory == nullptr || dwrite_factory == nullptr) {
        throw std::invalid_argument("Renderer requires valid Direct2D and DirectWrite factories");
    }
    CreateTextFormats();
}

void Renderer::CreateTextFormats() {
    card_label_format_ = CreateTextFormat(dwrite_factory_.Get(), 13.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    card_value_format_ = CreateTextFormat(dwrite_factory_.Get(), 20.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    card_subtitle_format_ = CreateTextFormat(dwrite_factory_.Get(), 10.0F, DWRITE_FONT_WEIGHT_NORMAL);
    detail_title_format_ = CreateTextFormat(dwrite_factory_.Get(), 25.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    detail_subtitle_format_ = CreateTextFormat(dwrite_factory_.Get(), 12.0F, DWRITE_FONT_WEIGHT_NORMAL);
    ThrowIfFailed(
        detail_subtitle_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING),
        "SetTextAlignment failed");
    graph_label_format_ = CreateTextFormat(dwrite_factory_.Get(), 10.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    logical_graph_label_format_ = CreateTextFormat(dwrite_factory_.Get(), 8.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    stat_label_format_ = CreateTextFormat(dwrite_factory_.Get(), 9.0F, DWRITE_FONT_WEIGHT_NORMAL);
    stat_value_format_ = CreateTextFormat(dwrite_factory_.Get(), 12.5F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

void Renderer::EnsureDeviceResources(HWND window, float dpi) {
    if (render_target_.Get() != nullptr) {
        return;
    }

    RECT client_rect{};
    GetClientRect(window, &client_rect);
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(client_rect.right - client_rect.left),
        static_cast<UINT32>(client_rect.bottom - client_rect.top));

    ThrowIfFailed(
        d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window, size),
            render_target_.GetAddressOf()),
        "CreateHwndRenderTarget failed");
    render_target_->SetDpi(dpi, dpi);

    const auto make_brush = [this](
                                const D2D1_COLOR_F& color,
                                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush) {
        ThrowIfFailed(
            render_target_->CreateSolidColorBrush(color, brush.GetAddressOf()),
            "CreateSolidColorBrush failed");
    };

    make_brush(D2D1::ColorF(0.065F, 0.078F, 0.105F, 1.0F), rail_brush_);
    make_brush(D2D1::ColorF(0.052F, 0.063F, 0.085F, 1.0F), detail_brush_);
    make_brush(D2D1::ColorF(0.105F, 0.125F, 0.165F, 1.0F), card_brush_);
    make_brush(D2D1::ColorF(0.135F, 0.165F, 0.215F, 1.0F), card_hover_brush_);
    make_brush(D2D1::ColorF(0.145F, 0.185F, 0.245F, 1.0F), card_selected_brush_);
    make_brush(D2D1::ColorF(0.040F, 0.052F, 0.071F, 1.0F), graph_background_brush_);
    make_brush(D2D1::ColorF(0.94F, 0.95F, 0.97F, 1.0F), primary_text_brush_);
    make_brush(D2D1::ColorF(0.70F, 0.74F, 0.80F, 1.0F), secondary_text_brush_);
    make_brush(D2D1::ColorF(0.49F, 0.54F, 0.62F, 1.0F), subtle_text_brush_);
    make_brush(D2D1::ColorF(0.18F, 0.21F, 0.28F, 1.0F), separator_brush_);
    make_brush(D2D1::ColorF(0.18F, 0.22F, 0.29F, 1.0F), grid_brush_);
    make_brush(D2D1::ColorF(0.27F, 0.68F, 0.95F, 1.0F), cpu_brush_);
    make_brush(D2D1::ColorF(0.27F, 0.68F, 0.95F, 0.14F), cpu_fill_brush_);
    make_brush(D2D1::ColorF(0.40F, 0.82F, 0.48F, 1.0F), gpu_brush_);
    make_brush(D2D1::ColorF(0.40F, 0.82F, 0.48F, 0.14F), gpu_fill_brush_);
}

void Renderer::DiscardDeviceResources() noexcept {
    gpu_fill_brush_.Reset();
    gpu_brush_.Reset();
    cpu_fill_brush_.Reset();
    cpu_brush_.Reset();
    grid_brush_.Reset();
    separator_brush_.Reset();
    subtle_text_brush_.Reset();
    secondary_text_brush_.Reset();
    primary_text_brush_.Reset();
    graph_background_brush_.Reset();
    card_selected_brush_.Reset();
    card_hover_brush_.Reset();
    card_brush_.Reset();
    detail_brush_.Reset();
    rail_brush_.Reset();
    render_target_.Reset();
}

void Renderer::Resize(UINT width, UINT height) {
    if (render_target_.Get() != nullptr && width > 0 && height > 0) {
        (void)render_target_->Resize(D2D1::SizeU(width, height));
    }
}

void Renderer::SetDpi(float dpi) {
    if (render_target_.Get() != nullptr) {
        render_target_->SetDpi(dpi, dpi);
    }
}

void Renderer::Draw(
    HWND window,
    float dpi,
    const UiState& state,
    const model::PerformanceSnapshot& performance,
    const std::wstring& gpu_name,
    const std::wstring& gpu_status) {
    EnsureDeviceResources(window, dpi);

    render_target_->BeginDraw();
    const D2D1_SIZE_F client_size = render_target_->GetSize();
    const Layout layout = CalculateLayout(client_size, state.IsExpanded());

    render_target_->Clear(D2D1::ColorF(0.052F, 0.063F, 0.085F, 1.0F));
    DrawComponentRail(layout, state, performance, gpu_name);

    if (state.IsExpanded()) {
        render_target_->FillRectangle(layout.detail_pane, detail_brush_.Get());
        render_target_->DrawLine(
            D2D1::Point2F(layout.detail_pane.left, 0.0F),
            D2D1::Point2F(layout.detail_pane.left, client_size.height),
            separator_brush_.Get(),
            1.0F);

        if (state.selected == Component::Cpu) {
            DrawCpuDetail(layout.detail_pane, performance);
        } else if (state.selected == Component::Gpu) {
            DrawGpuDetail(layout.detail_pane, performance, gpu_name, gpu_status);
        }
    }

    const HRESULT result = render_target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    } else {
        ThrowIfFailed(result, "Direct2D EndDraw failed");
    }
}

void Renderer::DrawComponentRail(
    const Layout& layout,
    const UiState& state,
    const model::PerformanceSnapshot& performance,
    const std::wstring& gpu_name) {
    render_target_->FillRectangle(layout.component_rail, rail_brush_.Get());

    DrawComponentCard(
        layout.cpu_card,
        Component::Cpu,
        state,
        performance.cpu_total,
        performance,
        L"CPU",
        CpuDisplayName(performance));

    const std::wstring gpu_subtitle = gpu_name.empty() ? L"NVML unavailable" : gpu_name;
    DrawComponentCard(
        layout.gpu_card,
        Component::Gpu,
        state,
        performance.gpu_total,
        performance,
        L"GPU",
        gpu_subtitle);
}

void Renderer::DrawComponentCard(
    const D2D1_RECT_F& bounds,
    Component component,
    const UiState& state,
    const model::MetricSeriesSnapshot& series,
    const model::PerformanceSnapshot& performance,
    std::wstring_view title,
    std::wstring_view subtitle) {
    ID2D1Brush* background = card_brush_.Get();
    if (state.selected == component) {
        background = card_selected_brush_.Get();
    } else if (state.hovered == component) {
        background = card_hover_brush_.Get();
    }

    render_target_->FillRoundedRectangle(Rounded(bounds), background);

    ID2D1SolidColorBrush* accent = component == Component::Cpu
        ? cpu_brush_.Get()
        : gpu_brush_.Get();
    ID2D1Brush* fill = component == Component::Cpu
        ? static_cast<ID2D1Brush*>(cpu_fill_brush_.Get())
        : static_cast<ID2D1Brush*>(gpu_fill_brush_.Get());

    render_target_->FillRoundedRectangle(
        Rounded(D2D1::RectF(bounds.left, bounds.top, bounds.left + 4.0F, bounds.bottom), 2.0F),
        accent);

    const std::wstring value = FormatPercentage(series.latest);
    DrawTextBlock(
        title,
        D2D1::RectF(bounds.left + 12.0F, bounds.top + 8.0F, bounds.left + 70.0F, bounds.top + 28.0F),
        card_label_format_.Get(),
        primary_text_brush_.Get());
    DrawTextBlock(
        value,
        D2D1::RectF(bounds.left + 12.0F, bounds.top + 31.0F, bounds.left + 74.0F, bounds.top + 59.0F),
        card_value_format_.Get(),
        primary_text_brush_.Get());

    const D2D1_RECT_F graph_bounds = D2D1::RectF(
        bounds.left + 80.0F,
        bounds.top + 11.0F,
        bounds.right - 9.0F,
        bounds.bottom - 21.0F);
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        graph_bounds,
        series.samples,
        performance.window_start,
        performance.window_end,
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            accent,
            fill,
            1.5F});

    DrawTextBlock(
        subtitle,
        D2D1::RectF(bounds.left + 80.0F, bounds.bottom - 18.0F, bounds.right - 8.0F, bounds.bottom - 4.0F),
        card_subtitle_format_.Get(),
        secondary_text_brush_.Get());
}

void Renderer::DrawCpuDetail(
    const D2D1_RECT_F& bounds,
    const model::PerformanceSnapshot& performance) {
    const float left = bounds.left + 22.0F;
    const float right = bounds.right - 22.0F;

    DrawTextBlock(
        L"CPU",
        D2D1::RectF(left, 15.0F, left + 110.0F, 48.0F),
        detail_title_format_.Get(),
        primary_text_brush_.Get());
    DrawTextBlock(
        CpuDisplayName(performance),
        D2D1::RectF(left + 120.0F, 19.0F, right, 43.0F),
        detail_subtitle_format_.Get(),
        secondary_text_brush_.Get());

    DrawTextBlock(
        L"Logical processors",
        D2D1::RectF(left, 54.0F, right - 55.0F, 70.0F),
        graph_label_format_.Get(),
        secondary_text_brush_.Get());
    DrawTextBlock(
        L"100%",
        D2D1::RectF(right - 55.0F, 54.0F, right, 70.0F),
        detail_subtitle_format_.Get(),
        subtle_text_brush_.Get());

    DrawCpuLogicalProcessorGrid(
        D2D1::RectF(left, 73.0F, right, 318.0F),
        performance);

    constexpr float stat_gap = 8.0F;
    constexpr std::size_t stat_count = 5;
    const float stat_top = 329.0F;
    const float stat_bottom = 379.0F;
    const float stat_width =
        (right - left - (stat_gap * static_cast<float>(stat_count - 1))) /
        static_cast<float>(stat_count);

    const auto stat_bounds = [&](std::size_t index) {
        const float stat_left = left +
            (static_cast<float>(index) * (stat_width + stat_gap));
        return D2D1::RectF(
            stat_left,
            stat_top,
            index + 1 == stat_count ? right : stat_left + stat_width,
            stat_bottom);
    };

    DrawCompactStat(
        stat_bounds(0),
        L"Utilization",
        FormatPercentage(performance.cpu_total.latest));
    DrawCompactStat(
        stat_bounds(1),
        L"Speed",
        FormatGigahertz(performance.cpu_info.current_speed_ghz));
    DrawCompactStat(
        stat_bounds(2),
        L"Processes",
        FormatCount(performance.cpu_info.process_count));
    DrawCompactStat(
        stat_bounds(3),
        L"Threads",
        FormatCount(performance.cpu_info.thread_count));
    DrawCompactStat(
        stat_bounds(4),
        L"Uptime",
        FormatUptime(performance.cpu_info.system_uptime));
}

void Renderer::DrawCpuLogicalProcessorGrid(
    const D2D1_RECT_F& bounds,
    const model::PerformanceSnapshot& performance) {
    const std::size_t count = performance.cpu_logical_processors.size();
    if (count == 0) {
        DrawGraph(
            render_target_.Get(),
            d2d_factory_.Get(),
            bounds,
            performance.cpu_total.samples,
            performance.window_start,
            performance.window_end,
            GraphStyle{
                graph_background_brush_.Get(),
                grid_brush_.Get(),
                cpu_brush_.Get(),
                cpu_fill_brush_.Get(),
                2.0F});
        return;
    }

    std::size_t columns = 1;
    if (count <= 4) {
        columns = count;
    } else if (count <= 8) {
        columns = 4;
    } else if (count <= 16) {
        columns = 4;
    } else if (count <= 24) {
        columns = 6;
    } else if (count <= 32) {
        columns = 8;
    } else if (count <= 64) {
        columns = 8;
    } else {
        columns = 12;
    }

    const std::size_t rows = (count + columns - 1) / columns;
    constexpr float gap = 4.0F;
    const float cell_width =
        (bounds.right - bounds.left - (gap * static_cast<float>(columns - 1))) /
        static_cast<float>(columns);
    const float cell_height =
        (bounds.bottom - bounds.top - (gap * static_cast<float>(rows - 1))) /
        static_cast<float>(rows);

    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t row = index / columns;
        const std::size_t column = index % columns;
        const float x = bounds.left + static_cast<float>(column) * (cell_width + gap);
        const float y = bounds.top + static_cast<float>(row) * (cell_height + gap);
        const D2D1_RECT_F cell = D2D1::RectF(x, y, x + cell_width, y + cell_height);

        DrawGraph(
            render_target_.Get(),
            d2d_factory_.Get(),
            cell,
            performance.cpu_logical_processors[index].samples,
            performance.window_start,
            performance.window_end,
            GraphStyle{
                graph_background_brush_.Get(),
                nullptr,
                cpu_brush_.Get(),
                nullptr,
                1.0F});
        render_target_->DrawRectangle(cell, separator_brush_.Get(), 1.0F);

        DrawTextBlock(
            std::to_wstring(index),
            D2D1::RectF(cell.left + 3.0F, cell.top + 2.0F, cell.right - 2.0F, cell.top + 13.0F),
            logical_graph_label_format_.Get(),
            secondary_text_brush_.Get());
    }
}

void Renderer::DrawGpuDetail(
    const D2D1_RECT_F& bounds,
    const model::PerformanceSnapshot& performance,
    const std::wstring& gpu_name,
    const std::wstring& gpu_status) {
    const float left = bounds.left + 22.0F;
    const float right = bounds.right - 22.0F;
    const std::wstring& subtitle = gpu_name.empty() ? gpu_status : gpu_name;

    DrawTextBlock(
        L"GPU",
        D2D1::RectF(left, 15.0F, left + 110.0F, 48.0F),
        detail_title_format_.Get(),
        primary_text_brush_.Get());
    DrawTextBlock(
        subtitle,
        D2D1::RectF(left + 120.0F, 19.0F, right, 43.0F),
        detail_subtitle_format_.Get(),
        secondary_text_brush_.Get());

    DrawTextBlock(
        L"GPU utilization",
        D2D1::RectF(left, 54.0F, right - 55.0F, 70.0F),
        graph_label_format_.Get(),
        secondary_text_brush_.Get());
    DrawTextBlock(
        L"100%",
        D2D1::RectF(right - 55.0F, 54.0F, right, 70.0F),
        detail_subtitle_format_.Get(),
        subtle_text_brush_.Get());

    const D2D1_RECT_F gpu_graph = D2D1::RectF(left, 73.0F, right, 238.0F);
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        gpu_graph,
        performance.gpu_total.samples,
        performance.window_start,
        performance.window_end,
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            gpu_brush_.Get(),
            gpu_fill_brush_.Get(),
            2.0F});

    DrawTextBlock(
        L"Dedicated GPU memory",
        D2D1::RectF(left, 249.0F, right, 264.0F),
        graph_label_format_.Get(),
        secondary_text_brush_.Get());
    const D2D1_RECT_F vram_graph = D2D1::RectF(left, 267.0F, right, 311.0F);
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        vram_graph,
        performance.gpu_memory.samples,
        performance.window_start,
        performance.window_end,
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            gpu_brush_.Get(),
            gpu_fill_brush_.Get(),
            1.5F});

    constexpr float stat_gap = 8.0F;
    const float stat_top = 329.0F;
    const float stat_bottom = 379.0F;
    const float stat_width = (right - left - (stat_gap * 4.0F)) / 5.0F;

    const std::wstring utilization = FormatPercentage(performance.gpu_total.latest);
    const std::wstring memory = FormatPercentage(performance.gpu_memory.latest);
    DrawCompactStat(
        D2D1::RectF(left, stat_top, left + stat_width, stat_bottom),
        L"Utilization",
        utilization);
    DrawCompactStat(
        D2D1::RectF(left + stat_width + stat_gap, stat_top,
                    left + (stat_width * 2.0F) + stat_gap, stat_bottom),
        L"VRAM usage",
        memory);
    DrawCompactStat(
        D2D1::RectF(left + (stat_width * 2.0F) + (stat_gap * 2.0F), stat_top,
                    left + (stat_width * 3.0F) + (stat_gap * 2.0F), stat_bottom),
        L"Temperature",
        L"—");
    DrawCompactStat(
        D2D1::RectF(left + (stat_width * 3.0F) + (stat_gap * 3.0F), stat_top,
                    left + (stat_width * 4.0F) + (stat_gap * 3.0F), stat_bottom),
        L"Power",
        L"—");
    DrawCompactStat(
        D2D1::RectF(left + (stat_width * 4.0F) + (stat_gap * 4.0F), stat_top,
                    right, stat_bottom),
        L"Graphics clock",
        L"—");
}

void Renderer::DrawCompactStat(
    const D2D1_RECT_F& bounds,
    std::wstring_view label,
    std::wstring_view value) {
    DrawTextBlock(
        label,
        D2D1::RectF(bounds.left, bounds.top + 3.0F, bounds.right, bounds.top + 17.0F),
        stat_label_format_.Get(),
        secondary_text_brush_.Get());
    DrawTextBlock(
        value,
        D2D1::RectF(bounds.left, bounds.top + 20.0F, bounds.right, bounds.bottom),
        stat_value_format_.Get(),
        primary_text_brush_.Get());
}

void Renderer::DrawTextBlock(
    std::wstring_view text,
    const D2D1_RECT_F& bounds,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    D2D1_DRAW_TEXT_OPTIONS options) const {
    if (text.empty() || format == nullptr || brush == nullptr) {
        return;
    }

    render_target_->DrawTextW(
        text.data(),
        static_cast<UINT32>(text.size()),
        format,
        bounds,
        brush,
        options);
}

} // namespace perfmon::ui
