#include "presentation/widget_window.h"

#include "application/app_service.h"
#include "platform/windows/window_class.h"
#include "presentation/layout.h"
#include "presentation/renderer.h"
#include "presentation/theme.h"

#include <algorithm>
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
    RECT client{};
    GetClientRect(window_, &client);
    const auto layout = calculate_layout(
        {static_cast<float>(client.right), static_cast<float>(client.bottom)},
        static_cast<float>(dpi_), LayoutMode::compact);
    const auto palette = resolve_theme(service_.snapshot().settings.theme, read_system_theme());
    renderer_->draw(layout, palette);
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
