#include "renderer.hpp"

#include "graph_renderer.hpp"

#include <array>
#include <stdexcept>

namespace perfmon::ui {
namespace {

constexpr std::array<float, 36> kCpuHistory{
    18.0F, 21.0F, 20.0F, 32.0F, 45.0F, 38.0F, 25.0F, 29.0F, 31.0F,
    52.0F, 61.0F, 54.0F, 42.0F, 36.0F, 27.0F, 33.0F, 46.0F, 41.0F,
    35.0F, 24.0F, 28.0F, 34.0F, 31.0F, 39.0F, 55.0F, 62.0F, 49.0F,
    44.0F, 38.0F, 35.0F, 40.0F, 47.0F, 45.0F, 39.0F, 41.0F, 42.0F,
};

constexpr std::array<float, 36> kGpuHistory{
    4.0F, 5.0F, 5.0F, 8.0F, 12.0F, 25.0F, 37.0F, 32.0F, 44.0F,
    58.0F, 69.0F, 63.0F, 49.0F, 42.0F, 31.0F, 26.0F, 21.0F, 19.0F,
    16.0F, 12.0F, 9.0F, 11.0F, 14.0F, 18.0F, 20.0F, 24.0F, 29.0F,
    27.0F, 24.0F, 22.0F, 19.0F, 18.0F, 16.0F, 18.0F, 16.0F, 17.0F,
};

constexpr std::array<float, 36> kVramHistory{
    32.0F, 32.0F, 33.0F, 33.0F, 34.0F, 35.0F, 36.0F, 38.0F, 41.0F,
    43.0F, 45.0F, 47.0F, 48.0F, 49.0F, 49.0F, 50.0F, 50.0F, 50.0F,
    51.0F, 51.0F, 51.0F, 52.0F, 52.0F, 52.0F, 51.0F, 51.0F, 50.0F,
    50.0F, 49.0F, 49.0F, 49.0F, 48.0F, 48.0F, 48.0F, 48.0F, 48.0F,
};

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
    card_value_format_ = CreateTextFormat(dwrite_factory_.Get(), 24.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    card_subtitle_format_ = CreateTextFormat(dwrite_factory_.Get(), 10.0F, DWRITE_FONT_WEIGHT_NORMAL);
    detail_title_format_ = CreateTextFormat(dwrite_factory_.Get(), 25.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    detail_subtitle_format_ = CreateTextFormat(dwrite_factory_.Get(), 12.0F, DWRITE_FONT_WEIGHT_NORMAL);
    body_format_ = CreateTextFormat(dwrite_factory_.Get(), 13.0F, DWRITE_FONT_WEIGHT_NORMAL);
    graph_label_format_ = CreateTextFormat(dwrite_factory_.Get(), 11.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    stat_label_format_ = CreateTextFormat(dwrite_factory_.Get(), 11.0F, DWRITE_FONT_WEIGHT_NORMAL);
    stat_value_format_ = CreateTextFormat(dwrite_factory_.Get(), 15.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD);
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
    const std::wstring& gpu_name,
    const std::wstring& gpu_status) {
    EnsureDeviceResources(window, dpi);

    render_target_->BeginDraw();
    const D2D1_SIZE_F client_size = render_target_->GetSize();
    const Layout layout = CalculateLayout(client_size, state.IsExpanded());

    render_target_->Clear(D2D1::ColorF(0.052F, 0.063F, 0.085F, 1.0F));
    DrawComponentRail(layout, state, gpu_name);

    if (state.IsExpanded()) {
        render_target_->FillRectangle(layout.detail_pane, detail_brush_.Get());
        render_target_->DrawLine(
            D2D1::Point2F(layout.detail_pane.left, 0.0F),
            D2D1::Point2F(layout.detail_pane.left, client_size.height),
            separator_brush_.Get(),
            1.0F);

        if (state.selected == Component::Cpu) {
            DrawCpuDetail(layout.detail_pane);
        } else if (state.selected == Component::Gpu) {
            DrawGpuDetail(layout.detail_pane, gpu_name, gpu_status);
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
    const std::wstring& gpu_name) {
    render_target_->FillRectangle(layout.component_rail, rail_brush_.Get());

    DrawComponentCard(
        layout.cpu_card,
        Component::Cpu,
        state,
        L"CPU",
        L"42%",
        L"AMD Ryzen 9 5900X");

    const std::wstring gpu_subtitle = gpu_name.empty() ? L"NVML unavailable" : gpu_name;
    DrawComponentCard(
        layout.gpu_card,
        Component::Gpu,
        state,
        L"GPU",
        L"17%",
        gpu_subtitle);
}

void Renderer::DrawComponentCard(
    const D2D1_RECT_F& bounds,
    Component component,
    const UiState& state,
    std::wstring_view title,
    std::wstring_view value,
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
    render_target_->FillRoundedRectangle(
        Rounded(D2D1::RectF(bounds.left, bounds.top, bounds.left + 4.0F, bounds.bottom), 2.0F),
        accent);

    DrawTextBlock(
        title,
        D2D1::RectF(bounds.left + 12.0F, bounds.top + 8.0F, bounds.left + 70.0F, bounds.top + 28.0F),
        card_label_format_.Get(),
        primary_text_brush_.Get());
    DrawTextBlock(
        value,
        D2D1::RectF(bounds.left + 12.0F, bounds.top + 29.0F, bounds.left + 74.0F, bounds.top + 62.0F),
        card_value_format_.Get(),
        primary_text_brush_.Get());

    const D2D1_RECT_F graph_bounds = D2D1::RectF(
        bounds.left + 80.0F,
        bounds.top + 11.0F,
        bounds.right - 9.0F,
        bounds.bottom - 21.0F);
    const bool is_cpu = component == Component::Cpu;
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        graph_bounds,
        is_cpu ? std::span<const float>(kCpuHistory) : std::span<const float>(kGpuHistory),
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            accent,
            is_cpu ? static_cast<ID2D1Brush*>(cpu_fill_brush_.Get())
                   : static_cast<ID2D1Brush*>(gpu_fill_brush_.Get()),
            1.5F});

    DrawTextBlock(
        subtitle,
        D2D1::RectF(bounds.left + 80.0F, bounds.bottom - 18.0F, bounds.right - 8.0F, bounds.bottom - 4.0F),
        card_subtitle_format_.Get(),
        secondary_text_brush_.Get());
}

void Renderer::DrawCpuDetail(const D2D1_RECT_F& bounds) {
    const float left = bounds.left + 22.0F;
    const float right = bounds.right - 22.0F;
    const float stats_left = right - 164.0F;

    DrawTextBlock(
        L"CPU",
        D2D1::RectF(left, 17.0F, right, 50.0F),
        detail_title_format_.Get(),
        primary_text_brush_.Get());
    DrawTextBlock(
        L"AMD Ryzen 9 5900X",
        D2D1::RectF(left, 49.0F, right, 68.0F),
        detail_subtitle_format_.Get(),
        secondary_text_brush_.Get());

    DrawTextBlock(
        L"Utilization",
        D2D1::RectF(left, 78.0F, stats_left - 16.0F, 95.0F),
        graph_label_format_.Get(),
        secondary_text_brush_.Get());
    DrawTextBlock(
        L"100%",
        D2D1::RectF(stats_left - 60.0F, 78.0F, stats_left - 16.0F, 95.0F),
        graph_label_format_.Get(),
        subtle_text_brush_.Get());

    const D2D1_RECT_F graph_bounds = D2D1::RectF(left, 99.0F, stats_left - 16.0F, 271.0F);
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        graph_bounds,
        kCpuHistory,
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            cpu_brush_.Get(),
            cpu_fill_brush_.Get(),
            2.0F});

    DrawTextBlock(
        L"60 seconds",
        D2D1::RectF(left, 275.0F, stats_left - 16.0F, 292.0F),
        graph_label_format_.Get(),
        subtle_text_brush_.Get());

    DrawStat(stats_left, 83.0F, 164.0F, L"Utilization", L"42%");
    DrawStat(stats_left, 141.0F, 164.0F, L"Physical cores", L"12");
    DrawStat(stats_left, 199.0F, 164.0F, L"Logical processors", L"24");
    DrawStat(stats_left, 257.0F, 164.0F, L"System uptime", L"3:42:18");

    DrawTextBlock(
        L"Static prototype data — live CPU sampling is introduced in Phase 4.",
        D2D1::RectF(left, 346.0F, right, 369.0F),
        body_format_.Get(),
        subtle_text_brush_.Get());
}

void Renderer::DrawGpuDetail(
    const D2D1_RECT_F& bounds,
    const std::wstring& gpu_name,
    const std::wstring& gpu_status) {
    const float left = bounds.left + 22.0F;
    const float right = bounds.right - 22.0F;
    const float stats_left = right - 164.0F;
    const std::wstring& subtitle = gpu_name.empty() ? gpu_status : gpu_name;

    DrawTextBlock(
        L"GPU",
        D2D1::RectF(left, 17.0F, right, 50.0F),
        detail_title_format_.Get(),
        primary_text_brush_.Get());
    DrawTextBlock(
        subtitle,
        D2D1::RectF(left, 49.0F, right, 68.0F),
        detail_subtitle_format_.Get(),
        secondary_text_brush_.Get());

    DrawTextBlock(
        L"GPU utilization",
        D2D1::RectF(left, 78.0F, stats_left - 16.0F, 95.0F),
        graph_label_format_.Get(),
        secondary_text_brush_.Get());
    const D2D1_RECT_F gpu_graph = D2D1::RectF(left, 99.0F, stats_left - 16.0F, 218.0F);
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        gpu_graph,
        kGpuHistory,
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            gpu_brush_.Get(),
            gpu_fill_brush_.Get(),
            2.0F});

    DrawTextBlock(
        L"Dedicated GPU memory",
        D2D1::RectF(left, 237.0F, stats_left - 16.0F, 254.0F),
        graph_label_format_.Get(),
        secondary_text_brush_.Get());
    const D2D1_RECT_F vram_graph = D2D1::RectF(left, 258.0F, stats_left - 16.0F, 329.0F);
    DrawGraph(
        render_target_.Get(),
        d2d_factory_.Get(),
        vram_graph,
        kVramHistory,
        GraphStyle{
            graph_background_brush_.Get(),
            grid_brush_.Get(),
            gpu_brush_.Get(),
            gpu_fill_brush_.Get(),
            1.7F});

    DrawStat(stats_left, 83.0F, 164.0F, L"Utilization", L"17%");
    DrawStat(stats_left, 136.0F, 164.0F, L"Dedicated memory", L"5.8 / 12.0 GB");
    DrawStat(stats_left, 189.0F, 164.0F, L"Temperature", L"48 C");
    DrawStat(stats_left, 242.0F, 164.0F, L"Power", L"86 W");
    DrawStat(stats_left, 295.0F, 164.0F, L"Graphics clock", L"2,100 MHz");

    DrawTextBlock(
        L"Static prototype data — live NVML metrics are introduced in Phase 5.",
        D2D1::RectF(left, 346.0F, right, 369.0F),
        body_format_.Get(),
        subtle_text_brush_.Get());
}

void Renderer::DrawStat(
    float left,
    float top,
    float width,
    std::wstring_view label,
    std::wstring_view value) {
    const D2D1_RECT_F panel = D2D1::RectF(left, top, left + width, top + 46.0F);
    render_target_->FillRoundedRectangle(Rounded(panel, 6.0F), card_brush_.Get());
    DrawTextBlock(
        label,
        D2D1::RectF(left + 10.0F, top + 6.0F, left + width - 8.0F, top + 21.0F),
        stat_label_format_.Get(),
        secondary_text_brush_.Get());
    DrawTextBlock(
        value,
        D2D1::RectF(left + 10.0F, top + 22.0F, left + width - 8.0F, top + 43.0F),
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
