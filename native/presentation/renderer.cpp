#include "presentation/renderer.h"

#include "presentation/task_list_view.h"

#include <algorithm>
#include <array>
#include <string>

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
    danger_.Reset();
    target_.Reset();
    state_.mark_resources_discarded();
}

void Renderer::resize(UINT width, UINT height) {
    if (target_) {
        const auto result = target_->Resize(D2D1::SizeU(width, height));
        if (result == D2DERR_RECREATE_TARGET) discard_device_resources();
    }
}

void Renderer::draw(
    const LayoutResult& layout,
    const ThemePalette& palette,
    const ViewModel& model,
    ViewKind view,
    const std::vector<std::wstring>& selected_ids,
    float scroll_y) {
    if (!create_device_resources()) return;
    foreground_.Reset();
    muted_.Reset();
    surface_.Reset();
    border_.Reset();
    danger_.Reset();
    if (FAILED(target_->CreateSolidColorBrush(color(palette.foreground), &foreground_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.muted), &muted_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.surface), &surface_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.border), &border_)) ||
        FAILED(target_->CreateSolidColorBrush(color(palette.danger), &danger_))) {
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
    if (body_format_ && foreground_ && muted_ && surface_ && border_ && danger_) {
        constexpr std::array<ViewKind, 4> views{
            ViewKind::today, ViewKind::week, ViewKind::all, ViewKind::done};
        constexpr std::array<const wchar_t*, 4> names{L"今天", L"本周", L"全部", L"已完成"};
        const auto tab_width = layout.tabs.width / static_cast<float>(views.size());
        for (std::size_t index = 0; index < views.size(); ++index) {
            const auto x = layout.tabs.x + tab_width * static_cast<float>(index);
            const auto tab = D2D1::RoundedRect(D2D1::RectF(
                x, layout.tabs.y, x + tab_width - 4, layout.tabs.bottom()), 8, 8);
            const bool active = views[index] == view;
            target_->FillRoundedRectangle(tab, active ? surface_.Get() : border_.Get());
            const auto count = index == 0 ? model.counts.today
                : index == 1 ? model.counts.week
                : index == 2 ? model.counts.all
                             : model.counts.done;
            const auto label = std::wstring{names[index]} + L" " + std::to_wstring(count);
            target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                body_format_.Get(), D2D1::RectF(x + 2, layout.tabs.y + 4,
                    x + tab_width - 6, layout.tabs.bottom() - 2),
                active ? foreground_.Get() : muted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        constexpr float row_height = 58.0F;
        const auto visible = calculate_visible_range(scroll_y,
            layout.task_list.height, row_height, model.rows.size(), 2);
        target_->PushAxisAlignedClip(D2D1::RectF(
            layout.task_list.x, layout.task_list.y,
            layout.task_list.right(), layout.task_list.bottom()),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (auto index = visible.first; index < visible.last; ++index) {
            const auto& row = model.rows[index];
            const auto y = layout.task_list.y +
                static_cast<float>(index) * row_height - scroll_y;
            const auto row_rect = D2D1::RoundedRect(D2D1::RectF(
                layout.task_list.x, y + 2, layout.task_list.right(), y + row_height - 2), 8, 8);
            const bool selected = std::find(selected_ids.begin(), selected_ids.end(), row.id) !=
                selected_ids.end();
            target_->FillRoundedRectangle(row_rect,
                selected || row.status == TaskStatus::done ? surface_.Get() : border_.Get());
            target_->DrawRoundedRectangle(row_rect, border_.Get(), 1.0F);
            const auto checkbox = D2D1::RoundedRect(D2D1::RectF(
                layout.task_list.x + 10, y + 17, layout.task_list.x + 32, y + 39), 6, 6);
            target_->DrawRoundedRectangle(checkbox,
                row.status == TaskStatus::done ? muted_.Get() : foreground_.Get(), 1.5F);
            if (row.overdue) {
                const auto overdue = D2D1::RectF(
                    layout.task_list.x + 39, y + 13, layout.task_list.x + 42, y + 45);
                target_->FillRectangle(overdue, danger_.Get());
            }
            const auto title = D2D1::RectF(
                layout.task_list.x + 50, y + 8,
                layout.task_list.right() - 56, y + row_height - 8);
            target_->DrawTextW(row.title.c_str(), static_cast<UINT32>(row.title.size()),
                body_format_.Get(), title,
                row.status == TaskStatus::done ? muted_.Get() : foreground_.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
            const auto remove = D2D1::RectF(
                layout.task_list.right() - 48, y + 8,
                layout.task_list.right() - 24, y + 40);
            target_->DrawTextW(L"×", 1, body_format_.Get(), remove, muted_.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
            const auto grip = D2D1::RectF(
                layout.task_list.right() - 24, y + 8,
                layout.task_list.right() - 4, y + 40);
            target_->DrawTextW(L"⋮", 1, body_format_.Get(), grip, muted_.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (model.rows.empty()) {
            const wchar_t* empty_text = model.counts.all == 0
                ? L"还没有任务，先添加一项吧"
                : L"这个视图暂时没有任务";
            const auto empty = D2D1::RectF(layout.task_list.x + 8,
                layout.task_list.y + 12, layout.task_list.right() - 8,
                layout.task_list.bottom() - 8);
            target_->DrawTextW(empty_text, static_cast<UINT32>(wcslen(empty_text)),
                body_format_.Get(), empty, muted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        target_->PopAxisAlignedClip();
        if (model.counts.overdue > 0 && muted_) {
            const auto footer = D2D1::RectF(layout.footer.x, layout.footer.y,
                layout.footer.right(), layout.footer.bottom());
            const auto summary = L"逾期 " + std::to_wstring(model.counts.overdue) + L" 项";
            target_->DrawTextW(summary.c_str(), static_cast<UINT32>(summary.size()),
                body_format_.Get(), footer, muted_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    const auto result = target_->EndDraw();
    if (state_.finish_draw(result)) discard_device_resources();
}

}  // namespace desktop_todo
