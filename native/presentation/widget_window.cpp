#include "presentation/widget_window.h"

#include "application/app_service.h"
#include "platform/windows/window_class.h"
#include "presentation/layout.h"
#include "presentation/renderer.h"
#include "presentation/task_list_view.h"
#include "presentation/theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <windowsx.h>

namespace desktop_todo {
namespace {

constexpr wchar_t kWidgetWindowClass[] = L"DesktopTodoList.WidgetWindow.v2";
constexpr UINT_PTR kMaintenanceTimer = 1;

SystemTheme read_system_theme() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    DWORD light_theme = 1;
    DWORD light_theme_size = sizeof(light_theme);
    const auto theme_status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
        &light_theme, &light_theme_size);
    return {
        .dark = theme_status == ERROR_SUCCESS && light_theme == 0,
        .high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0,
        .reduced_motion = animations == FALSE};
}

Clock::time_point now() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
}

std::chrono::sys_days local_day(Clock::time_point instant) {
    constexpr std::int64_t windows_epoch_offset_ms = 11'644'473'600'000LL;
    const auto ticks = static_cast<ULONGLONG>(
        instant.time_since_epoch().count() + windows_epoch_offset_ms) * 10'000ULL;
    ULARGE_INTEGER value{};
    value.QuadPart = ticks;
    FILETIME utc_file_time{value.LowPart, value.HighPart};
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&utc_file_time, &utc) ||
        !SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) {
        return std::chrono::floor<std::chrono::days>(instant);
    }
    return std::chrono::sys_days{
        std::chrono::year{local.wYear} / local.wMonth / local.wDay};
}

LayoutResult current_layout(HWND window, UINT dpi) {
    RECT client{};
    GetClientRect(window, &client);
    return calculate_layout(
        {static_cast<float>(client.right), static_cast<float>(client.bottom)},
        static_cast<float>(dpi), LayoutMode::compact);
}

}  // namespace

WidgetWindow::WidgetWindow(AppService& service) : service_(service) {}

WidgetWindow::~WidgetWindow() { destroy(); }

bool WidgetWindow::create(HINSTANCE instance, int show_command) {
    window_class_ = std::make_unique<WindowClass>(
        instance, kWidgetWindowClass, &WidgetWindow::window_proc, nullptr);
    if (!window_class_->registered()) return false;

    dpi_ = GetDpiForSystem();
    const auto size = default_widget_size(static_cast<float>(dpi_));
    POINT origin{};
    MONITORINFO monitor_info{sizeof(monitor_info)};
    int x = 0;
    int y = 0;
    const auto primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(primary, &monitor_info)) {
        x = std::max(monitor_info.rcWork.left,
            monitor_info.rcWork.right - static_cast<int>(size.width) - 16);
        y = std::max(monitor_info.rcWork.top,
            monitor_info.rcWork.bottom - static_cast<int>(size.height) - 16);
    }
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        window_class_->name(), L"DesktopTodoList", WS_POPUP | WS_THICKFRAME,
        x, y,
        static_cast<int>(size.width), static_cast<int>(size.height),
        nullptr, nullptr, instance, this);
    if (window_ == nullptr) return false;
    dpi_ = GetDpiForWindow(window_);
    renderer_ = std::make_unique<Renderer>(window_);
    clamp_to_monitor();
    SetTimer(window_, kMaintenanceTimer, 250, nullptr);
    ShowWindow(window_, show_command == SW_HIDE ? SW_SHOWNORMAL : show_command);
    UpdateWindow(window_);
    return true;
}

void WidgetWindow::destroy() {
    if (window_ != nullptr) DestroyWindow(window_);
    renderer_.reset();
    window_class_.reset();
}

void WidgetWindow::invalidate() {
    if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
}

void WidgetWindow::show_and_activate() {
    if (window_ == nullptr) return;
    if (IsIconic(window_)) ShowWindow(window_, SW_RESTORE);
    ShowWindow(window_, SW_SHOWNORMAL);
    if (!SetForegroundWindow(window_)) {
        FLASHWINFO flash{sizeof(flash), window_, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&flash);
    }
}

HWND WidgetWindow::handle() const noexcept { return window_; }

LRESULT CALLBACK WidgetWindow::window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    WidgetWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<WidgetWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self != nullptr
        ? self->handle_message(message, wparam, lparam)
        : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT WidgetWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wparam != 0) return 0;
        break;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        return hit_test(point);
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = static_cast<LONG>(
            minimum_widget_width(static_cast<float>(dpi_)));
        limits->ptMinTrackSize.y = static_cast<LONG>(320.0F * dpi_ / 96.0F);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        invalidate();
        return 0;
    }
    case WM_DISPLAYCHANGE:
        clamp_to_monitor();
        return 0;
    case WM_SIZE:
        if (renderer_) renderer_->resize(LOWORD(lparam), HIWORD(lparam));
        invalidate();
        return 0;
    case WM_LBUTTONUP:
        handle_pointer({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window_, &point);
        const auto layout = current_layout(window_, dpi_);
        const auto logical_x = static_cast<float>(point.x) / (static_cast<float>(dpi_) / 96.0F);
        const auto logical_y = static_cast<float>(point.y) / (static_cast<float>(dpi_) / 96.0F);
        if (logical_x >= layout.task_list.x && logical_x < layout.task_list.right() &&
            logical_y >= layout.task_list.y && logical_y < layout.task_list.bottom()) {
            const auto wheel = GET_WHEEL_DELTA_WPARAM(wparam);
            scroll_y_ = std::max(0.0F, scroll_y_ -
                static_cast<float>(wheel) / WHEEL_DELTA * 3.0F * 58.0F);
            invalidate();
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        invalidate();
        return 0;
    case WM_TIMER:
        if (wparam == kMaintenanceTimer) static_cast<void>(service_.maintenance());
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        draw();
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        KillTimer(window_, kMaintenanceTimer);
        static_cast<void>(service_.flush());
        renderer_.reset();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void WidgetWindow::draw() {
    if (!renderer_) return;
    const auto& state = service_.snapshot();
    QuerySpec query{
        .view = current_view_,
        .week_starts_on = state.settings.week_starts_on,
        .local_day = local_day};
    std::vector<std::wstring> previous_ids;
    previous_ids.reserve(view_model_.rows.size());
    for (const auto& row : view_model_.rows) previous_ids.push_back(row.id);
    const auto anchor_index = static_cast<std::size_t>(scroll_y_ / 58.0F);
    const auto anchor_offset = std::fmod(scroll_y_, 58.0F);
    auto next_model = build_view_model(state, query, now());
    if (!previous_ids.empty() && !next_model.rows.empty()) {
        std::vector<std::wstring> next_ids;
        next_ids.reserve(next_model.rows.size());
        for (const auto& row : next_model.rows) next_ids.push_back(row.id);
        scroll_y_ = static_cast<float>(preserve_scroll_anchor(
            previous_ids, next_ids, anchor_index)) * 58.0F + anchor_offset;
    }
    view_model_ = std::move(next_model);
    const auto layout = current_layout(window_, dpi_);
    scroll_y_ = std::clamp(scroll_y_, 0.0F,
        std::max(0.0F, static_cast<float>(view_model_.rows.size()) * 58.0F -
            layout.task_list.height));
    const auto palette = resolve_theme(state.settings.theme, read_system_theme());
    renderer_->draw(layout, palette, view_model_, current_view_,
        service_.selection().snapshot().selected_ids, scroll_y_);
}

void WidgetWindow::handle_pointer(POINT client_point) {
    const auto scale = static_cast<float>(dpi_) / 96.0F;
    const PointF logical{
        static_cast<float>(client_point.x) / scale,
        static_cast<float>(client_point.y) / scale};
    const auto layout = current_layout(window_, dpi_);
    if (logical.x >= layout.tabs.x && logical.x < layout.tabs.right() &&
        logical.y >= layout.tabs.y && logical.y < layout.tabs.bottom()) {
        const auto tab_width = layout.tabs.width / 4.0F;
        const auto index = static_cast<std::size_t>(
            (logical.x - layout.tabs.x) / tab_width);
        constexpr ViewKind views[]{ViewKind::today, ViewKind::week, ViewKind::all, ViewKind::done};
        if (index < std::size(views) && current_view_ != views[index]) {
            current_view_ = views[index];
            scroll_y_ = 0;
            view_model_ = {};
            invalidate();
        }
        return;
    }
    if (logical.y < layout.task_list.y || logical.y >= layout.task_list.bottom()) return;
    const auto local_y = logical.y - layout.task_list.y + scroll_y_;
    if (local_y < 0) return;
    const auto index = static_cast<std::size_t>(local_y / 58.0F);
    if (index >= view_model_.rows.size()) return;
    const auto& row = view_model_.rows[index];
    const auto row_top = static_cast<float>(index) * 58.0F - scroll_y_;
    const RowHitZones zones{
        .row = {layout.task_list.x, layout.task_list.y + row_top,
            layout.task_list.width, 58.0F},
        .checkbox = {layout.task_list.x + 10, layout.task_list.y + row_top + 17, 22, 22},
        .title = {layout.task_list.x + 50, layout.task_list.y + row_top + 8,
            layout.task_list.width - 106, 42},
        .delete_button = {layout.task_list.right() - 48,
            layout.task_list.y + row_top + 8, 24, 32},
        .drag_handle = {layout.task_list.right() - 24,
            layout.task_list.y + row_top + 8, 16, 32}};
    const auto hit = hit_test_row(logical, zones);
    if (hit == RowHitArea::checkbox) {
        static_cast<void>(service_.set_completed(row.id, row.status != TaskStatus::done));
    } else if (hit == RowHitArea::row) {
        service_.selection().select_one(row.id);
        invalidate();
    }
}

void WidgetWindow::clamp_to_monitor() {
    if (window_ == nullptr) return;
    RECT window_rect{};
    GetWindowRect(window_, &window_rect);
    const auto monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    const auto clamped = desktop_todo::clamp_to_work_area(
        {window_rect.left, window_rect.top,
            window_rect.right - window_rect.left, window_rect.bottom - window_rect.top},
        {{info.rcWork.left, info.rcWork.top,
            info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top},
            static_cast<float>(dpi_)});
    SetWindowPos(window_, nullptr, clamped.x, clamped.y, clamped.width, clamped.height,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

LRESULT WidgetWindow::hit_test(POINT screen) const {
    RECT window{};
    GetWindowRect(window_, &window);
    const auto border = std::max(4, MulDiv(6, static_cast<int>(dpi_), 96));
    const bool left = screen.x < window.left + border;
    const bool right = screen.x >= window.right - border;
    const bool top = screen.y < window.top + border;
    const bool bottom = screen.y >= window.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    if (screen.y < window.top + MulDiv(56, static_cast<int>(dpi_), 96)) return HTCAPTION;
    return HTCLIENT;
}

}  // namespace desktop_todo
