#include "platform/windows/window_behavior.h"

#include <utility>

namespace desktop_todo {
namespace {

bool set_extended_style(HWND window, LONG_PTR style) noexcept {
    SetLastError(ERROR_SUCCESS);
    const auto previous = SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

bool update_frame(HWND window) noexcept {
    return SetWindowPos(window, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
}

bool position(HWND window, HWND insert_after) noexcept {
    return SetWindowPos(window, insert_after, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

}  // namespace

LONG_PTR click_through_extended_style(
    LONG_PTR current, bool enabled, WindowLayer layer) noexcept {
    constexpr LONG_PTR click_through_bits = WS_EX_LAYERED | WS_EX_TRANSPARENT;
    auto result = enabled ? current | click_through_bits : current & ~click_through_bits;
    if (enabled || layer == WindowLayer::bottom) result |= WS_EX_NOACTIVATE;
    else result &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    return result;
}

LONG_PTR layer_extended_style(
    LONG_PTR current, WindowLayer layer, bool click_through) noexcept {
    if (click_through || layer == WindowLayer::bottom) return current | WS_EX_NOACTIVATE;
    return current & ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
}

bool apply_window_layer(
    HWND window, WindowLayer layer, WindowLayer previous, bool click_through) noexcept {
    if (window == nullptr) return false;
    const auto old_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const auto new_style = layer_extended_style(old_style, layer, click_through);
    if (new_style != old_style && !set_extended_style(window, new_style)) return false;

    bool positioned = false;
    if (layer == WindowLayer::top) {
        positioned = position(window, HWND_TOPMOST);
    } else if (layer == WindowLayer::normal) {
        positioned = position(window, HWND_NOTOPMOST);
    } else {
        positioned = position(window, HWND_NOTOPMOST) && position(window, HWND_BOTTOM);
    }
    if (positioned && new_style == old_style) return true;
    if (!positioned) {
        const auto rollback_after = previous == WindowLayer::top ? HWND_TOPMOST :
            previous == WindowLayer::bottom ? HWND_BOTTOM : HWND_NOTOPMOST;
        static_cast<void>(position(window, rollback_after));
        if (new_style != old_style) static_cast<void>(set_extended_style(window, old_style));
        static_cast<void>(update_frame(window));
        return false;
    }
    if (update_frame(window)) return true;
    const auto rollback_after = previous == WindowLayer::top ? HWND_TOPMOST :
        previous == WindowLayer::bottom ? HWND_BOTTOM : HWND_NOTOPMOST;
    static_cast<void>(position(window, rollback_after));
    if (new_style != old_style) static_cast<void>(set_extended_style(window, old_style));
    static_cast<void>(update_frame(window));
    return false;
}

bool apply_window_click_through(HWND window, bool enabled, WindowLayer layer) noexcept {
    if (window == nullptr) return false;
    const auto old_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const auto new_style = click_through_extended_style(old_style, enabled, layer);
    if (new_style != old_style && !set_extended_style(window, new_style)) return false;
    if (enabled && !SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA)) {
        static_cast<void>(set_extended_style(window, old_style));
        static_cast<void>(update_frame(window));
        return false;
    }
    if (update_frame(window)) return true;
    static_cast<void>(set_extended_style(window, old_style));
    static_cast<void>(update_frame(window));
    return false;
}

WindowBehavior::WindowBehavior(WindowBehaviorApi api) : api_(std::move(api)) {}

bool WindowBehavior::initialize(WindowLayer layer, bool click_through) {
    if (!set_layer(layer)) return false;
    return !click_through || set_click_through(true);
}

bool WindowBehavior::set_layer(WindowLayer layer) {
    if (state_.layer == layer) return true;
    if (!api_.apply_layer(state_.layer, layer)) return false;
    state_.layer = layer;
    return true;
}

bool WindowBehavior::set_click_through(bool enabled) {
    if (state_.click_through == enabled) return true;
    if (enabled && (!api_.tray_available() || !api_.hotkey_available())) return false;
    if (!api_.apply_click_through(enabled)) return false;
    state_.click_through = enabled;
    state_.restore_at.reset();
    if (enabled && auto_restore_.has_value()) {
        state_.restore_at = api_.now() + *auto_restore_;
    }
    return true;
}

void WindowBehavior::restore_interaction() {
    static_cast<void>(set_click_through(false));
}

void WindowBehavior::on_foreground_changed() {
    if (state_.layer == WindowLayer::bottom) {
        static_cast<void>(api_.apply_layer(WindowLayer::bottom, WindowLayer::bottom));
    }
}

void WindowBehavior::set_auto_restore(std::optional<std::chrono::minutes> timeout) {
    if (timeout.has_value() && timeout->count() <= 0) timeout.reset();
    auto_restore_ = timeout;
    if (state_.click_through && auto_restore_.has_value()) {
        state_.restore_at = api_.now() + *auto_restore_;
    } else {
        state_.restore_at.reset();
    }
}

bool WindowBehavior::poll_timeout() {
    if (!state_.restore_at.has_value() || api_.now() < *state_.restore_at) return false;
    const auto was_enabled = state_.click_through;
    restore_interaction();
    return was_enabled && !state_.click_through;
}

const WindowBehaviorSnapshot& WindowBehavior::snapshot() const noexcept { return state_; }

}  // namespace desktop_todo
