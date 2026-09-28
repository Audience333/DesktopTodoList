#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>
#include <functional>
#include <filesystem>

#include "domain/types.h"
#include "presentation/pointer_controller.h"
#include "presentation/selection_toolbar.h"
#include "presentation/accessibility_provider.h"
#include "presentation/view_model.h"

namespace desktop_todo {

class AppService;
class DetailsPanel;
class PointerController;
class Renderer;
class SelectionToolbar;
class TextEditor;
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
    void hide_to_tray();
    [[nodiscard]] bool toggle_visibility();
    void begin_new_task();
    void set_file_drop_handler(std::function<void(const std::filesystem::path&)> handler);
    void apply_settings(const Settings& settings);
    [[nodiscard]] bool set_click_through(bool enabled);
    [[nodiscard]] HWND handle() const noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void draw();
    void clamp_to_monitor();
    void ensure_details_window_height();
    void commit_inline_title();
    void handle_pointer_down(POINT client_point);
    void handle_pointer_move(POINT client_point);
    void handle_pointer_up(POINT client_point);
    void handle_pointer(POINT client_point);
    [[nodiscard]] std::vector<std::wstring> visible_task_ids() const;
    [[nodiscard]] std::vector<PointerRowBounds> visible_task_bounds() const;
    void handle_pointer_action(const PointerAction& action);
    void handle_toolbar_action(SelectionToolbarAction action);
    void update_selection_toolbar();
    [[nodiscard]] AccessibilityTree accessibility_snapshot() const;
    void cancel_pointer_gesture();
    [[nodiscard]] LRESULT hit_test(POINT screen) const;

    AppService& service_;
    std::unique_ptr<WindowClass> window_class_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<DetailsPanel> details_panel_;
    std::unique_ptr<PointerController> pointer_controller_;
    std::unique_ptr<TextEditor> text_editor_;
    std::unique_ptr<SelectionToolbar> selection_toolbar_;
    std::unique_ptr<AccessibilityProvider> accessibility_provider_;
    HWND window_ = nullptr;
    UINT dpi_ = 96;
    ViewKind current_view_ = ViewKind::today;
    ViewModel view_model_;
    float scroll_y_ = 0;
    std::wstring search_text_;
    std::wstring details_task_id_;
    bool details_batch_ = false;
    std::optional<RectF> selection_band_;
    std::function<void(const std::filesystem::path&)> file_drop_handler_;
};

}  // namespace desktop_todo
