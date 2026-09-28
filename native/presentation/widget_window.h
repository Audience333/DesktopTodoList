#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>

#include "domain/types.h"
#include "presentation/view_model.h"

namespace desktop_todo {

class AppService;
class Renderer;
class WindowClass;

class WidgetWindow {
public:
    explicit WidgetWindow(AppService& service);
    ~WidgetWindow();
    WidgetWindow(const WidgetWindow&) = delete;
    WidgetWindow& operator=(const WidgetWindow&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, int show_command);
    void destroy();
    void invalidate();
    void show_and_activate();
    [[nodiscard]] HWND handle() const noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void draw();
    void clamp_to_monitor();
    void handle_pointer(POINT client_point);
    [[nodiscard]] LRESULT hit_test(POINT screen) const;

    AppService& service_;
    std::unique_ptr<WindowClass> window_class_;
    std::unique_ptr<Renderer> renderer_;
    HWND window_ = nullptr;
    UINT dpi_ = 96;
    ViewKind current_view_ = ViewKind::today;
    ViewModel view_model_;
    float scroll_y_ = 0;
};

}  // namespace desktop_todo
