#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "presentation/layout.h"
#include "presentation/view_model.h"
#include "presentation/theme.h"

#include <cstddef>

namespace desktop_todo {

class RendererState {
public:
    [[nodiscard]] bool resources_ready() const noexcept { return resources_ready_; }
    [[nodiscard]] std::size_t generation() const noexcept { return generation_; }
    void mark_resources_created() noexcept {
        resources_ready_ = true;
        ++generation_;
    }
    void mark_resources_discarded() noexcept { resources_ready_ = false; }
    [[nodiscard]] bool finish_draw(HRESULT result) noexcept {
        if (result != D2DERR_RECREATE_TARGET) return false;
        resources_ready_ = false;
        return true;
    }

private:
    bool resources_ready_ = false;
    std::size_t generation_ = 0;
};

class Renderer {
public:
    explicit Renderer(HWND window);
    [[nodiscard]] bool create_device_resources();
    void discard_device_resources();
    void resize(UINT width, UINT height);
    void draw(
        const LayoutResult& layout,
        const ThemePalette& palette,
        const ViewModel& model,
        ViewKind view,
        const std::vector<std::wstring>& selected_ids,
        float scroll_y);

private:
    static D2D1_COLOR_F color(std::uint32_t argb);

    HWND window_ = nullptr;
    RendererState state_;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> foreground_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> danger_;
};

}  // namespace desktop_todo
