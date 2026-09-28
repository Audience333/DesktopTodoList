#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "domain/types.h"

#include <chrono>
#include <functional>
#include <optional>

namespace desktop_todo {

struct WindowBehaviorSnapshot {
    WindowLayer layer = WindowLayer::normal;
    bool click_through = false;
    std::optional<std::chrono::steady_clock::time_point> restore_at;
};

struct WindowBehaviorApi {
    std::function<bool(WindowLayer, WindowLayer)> apply_layer;
    std::function<bool(bool)> apply_click_through;
    std::function<bool()> tray_available;
    std::function<bool()> hotkey_available;
    std::function<std::chrono::steady_clock::time_point()> now;
};

[[nodiscard]] LONG_PTR click_through_extended_style(
    LONG_PTR current, bool enabled, WindowLayer layer) noexcept;
[[nodiscard]] LONG_PTR layer_extended_style(
    LONG_PTR current, WindowLayer layer, bool click_through) noexcept;
[[nodiscard]] bool apply_window_layer(
    HWND window, WindowLayer layer, WindowLayer previous, bool click_through) noexcept;
[[nodiscard]] bool apply_window_click_through(
    HWND window, bool enabled, WindowLayer layer) noexcept;

class WindowBehavior {
public:
    explicit WindowBehavior(WindowBehaviorApi api);

    [[nodiscard]] bool initialize(WindowLayer layer, bool click_through);
    [[nodiscard]] bool set_layer(WindowLayer layer);
    [[nodiscard]] bool set_click_through(bool enabled);
    void restore_interaction();
    void on_foreground_changed();
    void set_auto_restore(std::optional<std::chrono::minutes> timeout);
    [[nodiscard]] bool poll_timeout();
    [[nodiscard]] const WindowBehaviorSnapshot& snapshot() const noexcept;

private:
    WindowBehaviorApi api_;
    WindowBehaviorSnapshot state_;
    std::optional<std::chrono::minutes> auto_restore_;
};

}  // namespace desktop_todo
