#include "presentation/renderer.h"

namespace desktop_todo {

Renderer::Renderer(HWND window) : window_(window) {
    static_cast<void>(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory), nullptr,
        reinterpret_cast<void**>(factory_.GetAddressOf())));
    static_cast<void>(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(write_factory_.GetAddressOf())));
    if (write_factory_) {
        static_cast<void>(write_factory_->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 22.0F, L"zh-CN", &title_format_));
        static_cast<void>(write_factory_->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0F, L"zh-CN", &body_format_));
    }
}

D2D1_COLOR_F Renderer::color(std::uint32_t argb) {
    return D2D1::ColorF(
        ((argb >> 16) & 0xFF) / 255.0F,
        ((argb >> 8) & 0xFF) / 255.0F,
        (argb & 0xFF) / 255.0F,
        ((argb >> 24) & 0xFF) / 255.0F);
}

bool Renderer::create_device_resources() {
    if (target_) return true;
    if (!factory_) return false;
    RECT client{};
    GetClientRect(window_, &client);
    const auto size = D2D1::SizeU(
        static_cast<UINT32>(client.right - client.left),
        static_cast<UINT32>(client.bottom - client.top));
    if (FAILED(factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window_, size), &target_))) return false;
    state_.mark_resources_created();
    return true;
}

void Renderer::discard_device_resources() {
    foreground_.Reset();
    muted_.Reset();
    surface_.Reset();
    border_.Reset();
    target_.Reset();
    state_.mark_resources_discarded();
}

void Renderer::resize(UINT width, UINT height) {
    if (target_) {
        const auto result = target_->Resize(D2D1::SizeU(width, height));
        if (result == D2DERR_RECREATE_TARGET) discard_device_resources();
    }
}

void Renderer::draw(const LayoutResult& layout, const ThemePalette& palette) {
    if (!create_device_resources()) return;
    foreground_.Reset();
    muted_.Reset();
    surface_.Reset();
    border_.Reset();
    if (FAILED(target_->CreateSolidColorBrush(color(palette.foreground), &foreground_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.muted), &muted_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.surface), &surface_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.border), &border_))) {
        discard_device_resources();
        return;
    }

    target_->SetTransform(D2D1::Matrix3x2F::Scale(layout.scale, layout.scale));
    target_->BeginDraw();
    target_->Clear(color(palette.background));
    const auto quick = D2D1::RoundedRect(
        D2D1::RectF(layout.quick_add.x, layout.quick_add.y,
            layout.quick_add.right(), layout.quick_add.bottom()), 10, 10);
    target_->FillRoundedRectangle(quick, surface_.Get());
    target_->DrawRoundedRectangle(quick, border_.Get(), 1.0F);
    if (title_format_ && foreground_) {
        const auto title = D2D1::RectF(20, 14, layout.logical_client.width - 20, 48);
        target_->DrawTextW(L"桌面待办", 4, title_format_.Get(), title, foreground_.Get());
    }
    if (body_format_ && muted_) {
        const auto hint = D2D1::RectF(layout.quick_add.x + 14, layout.quick_add.y + 10,
            layout.quick_add.right() - 14, layout.quick_add.bottom() - 8);
        target_->DrawTextW(L"添加任务…", 5, body_format_.Get(), hint, muted_.Get());
    }
    const auto result = target_->EndDraw();
    if (state_.finish_draw(result)) discard_device_resources();
}

}  // namespace desktop_todo
