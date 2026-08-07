#pragma once

#include "../model/history_store.hpp"
#include "../win32_headers.hpp"

#include "layout.hpp"
#include "ui_state.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace perfmon::ui {

class Renderer {
public:
    Renderer(ID2D1Factory* d2d_factory, IDWriteFactory* dwrite_factory);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Draw(
        HWND window,
        float dpi,
        const UiState& state,
        const model::PerformanceSnapshot& performance,
        const std::wstring& gpu_name,
        const std::wstring& gpu_status);
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);
    void DiscardDeviceResources() noexcept;

private:
    void CreateTextFormats();
    void EnsureDeviceResources(HWND window, float dpi);

    void DrawComponentRail(
        const Layout& layout,
        const UiState& state,
        const model::PerformanceSnapshot& performance,
        const std::wstring& gpu_name);
    void DrawComponentCard(
        const D2D1_RECT_F& bounds,
        Component component,
        const UiState& state,
        const model::MetricSeriesSnapshot& series,
        const model::PerformanceSnapshot& performance,
        std::wstring_view title,
        std::wstring_view subtitle);
    void DrawCpuDetail(
        const D2D1_RECT_F& bounds,
        const model::PerformanceSnapshot& performance);
    void DrawCpuLogicalProcessorGrid(
        const D2D1_RECT_F& bounds,
        const model::PerformanceSnapshot& performance);
    void DrawGpuDetail(
        const D2D1_RECT_F& bounds,
        const model::PerformanceSnapshot& performance,
        const std::wstring& gpu_name,
        const std::wstring& gpu_status);
    void DrawCompactStat(
        const D2D1_RECT_F& bounds,
        std::wstring_view label,
        std::wstring_view value);
    void DrawTextBlock(
        std::wstring_view text,
        const D2D1_RECT_F& bounds,
        IDWriteTextFormat* format,
        ID2D1Brush* brush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_CLIP) const;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> card_label_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> card_value_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> card_subtitle_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detail_title_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detail_subtitle_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> graph_label_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> logical_graph_label_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> stat_label_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> stat_value_format_;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rail_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> detail_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> card_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> card_hover_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> card_selected_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> graph_background_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> primary_text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> secondary_text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> subtle_text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> separator_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> grid_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cpu_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cpu_fill_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gpu_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gpu_fill_brush_;
};

} // namespace perfmon::ui
