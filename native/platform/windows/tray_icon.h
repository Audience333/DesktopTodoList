#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "domain/types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace desktop_todo {

enum class TrayCommand : UINT {
    toggle_visibility = 0x5101,
    new_task,
    layer_top,
    layer_normal,
    layer_bottom,
    toggle_interaction,
    toggle_close_behavior,
    settings,
    exit_application,
    pending_count
};

struct TrayMenuState {
    bool window_visible = true;
    std::uint32_t pending_count = 0;
    WindowLayer layer = WindowLayer::normal;
    bool interaction_enabled = true;
    bool interaction_toggle_available = true;
    bool close_to_tray = true;
};

struct TrayMenuItem {
    TrayCommand command;
    std::wstring label;
    bool checked = false;
    bool enabled = true;
    bool separator_before = false;
};

[[nodiscard]] std::vector<TrayMenuItem> build_tray_menu(const TrayMenuState& state);
[[nodiscard]] std::wstring tray_tooltip(const TrayMenuState& state);

enum class CloseDisposition { hide_to_tray, exit_application };
[[nodiscard]] CloseDisposition close_disposition(bool close_to_tray) noexcept;

struct TrayShellApi {
    std::function<bool()> add;
    std::function<bool(const TrayMenuState&)> update;
    std::function<bool()> remove;
};

class TrayIcon {
public:
    TrayIcon(HWND owner, UINT callback_message, TrayShellApi shell = {});
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    [[nodiscard]] bool install();
    void update_count(std::uint32_t pending_count);
    void update_interaction_state(bool enabled);
    void update_state(TrayMenuState state);
    void remove() noexcept;
    [[nodiscard]] bool handle_taskbar_created();
    [[nodiscard]] bool installed() const noexcept;
    void show_context_menu();
    [[nodiscard]] UINT taskbar_created_message() const noexcept;

private:
    HWND owner_ = nullptr;
    UINT callback_message_ = 0;
    UINT taskbar_created_message_ = 0;
    TrayShellApi shell_;
    TrayMenuState state_;
    bool installed_ = false;
};

}  // namespace desktop_todo
