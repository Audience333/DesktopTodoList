#include "platform/windows/tray_icon.h"

#include <shellapi.h>

#include <algorithm>
#include <array>

namespace desktop_todo {
namespace {

constexpr UINT kTrayIconId = 1;

NOTIFYICONDATAW notification_data(HWND owner, UINT callback_message,
    const TrayMenuState& state, UINT flags) {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = owner;
    data.uID = kTrayIconId;
    data.uFlags = flags;
    data.uCallbackMessage = callback_message;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    const auto tip = tray_tooltip(state);
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    return data;
}

TrayShellApi native_shell(HWND owner, UINT callback_message) {
    return {
        [owner, callback_message] {
            auto data = notification_data(owner, callback_message, {},
                NIF_MESSAGE | NIF_ICON | NIF_TIP);
            if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
            data.uVersion = NOTIFYICON_VERSION_4;
            if (Shell_NotifyIconW(NIM_SETVERSION, &data)) return true;
            data = notification_data(owner, callback_message, {}, 0);
            static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
            return false;
        },
        [owner, callback_message](const TrayMenuState& state) {
            auto data = notification_data(owner, callback_message, state, NIF_TIP);
            return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
        },
        [owner] {
            auto data = notification_data(owner, 0, {}, 0);
            return Shell_NotifyIconW(NIM_DELETE, &data) != FALSE;
        }};
}

}  // namespace

std::vector<TrayMenuItem> build_tray_menu(const TrayMenuState& state) {
    std::vector<TrayMenuItem> items;
    items.reserve(10);
    items.push_back({TrayCommand::toggle_visibility,
        state.window_visible ? L"隐藏窗口" : L"显示窗口", state.window_visible});
    items.push_back({TrayCommand::pending_count,
        L"待办事项: " + std::to_wstring(state.pending_count), false, false, true});
    items.push_back({TrayCommand::new_task, L"新建任务", false, true, true});
    items.push_back({TrayCommand::layer_top, L"置顶", state.layer == WindowLayer::top, true, true});
    items.push_back({TrayCommand::layer_normal, L"普通", state.layer == WindowLayer::normal, true});
    items.push_back({TrayCommand::layer_bottom, L"置底", state.layer == WindowLayer::bottom, true});
    items.push_back({TrayCommand::toggle_interaction,
        state.interaction_enabled ? L"开启鼠标穿透" : L"允许交互",
        state.interaction_enabled, state.interaction_toggle_available, true});
    items.push_back({TrayCommand::toggle_close_behavior, L"关闭时收起到托盘",
        state.close_to_tray, true, true});
    items.push_back({TrayCommand::settings, L"设置", false, true, true});
    items.push_back({TrayCommand::exit_application, L"退出", false, true, true});
    return items;
}

std::wstring tray_tooltip(const TrayMenuState& state) {
    const auto status = state.interaction_enabled ? L"允许交互" : L"鼠标穿透";
    auto result = L"DesktopTodoList | 待办 " + std::to_wstring(state.pending_count) +
        L" | " + status;
    if (result.size() >= 128) result.resize(127);
    return result;
}

CloseDisposition close_disposition(bool close_to_tray) noexcept {
    return close_to_tray ? CloseDisposition::hide_to_tray
                         : CloseDisposition::exit_application;
}

TrayIcon::TrayIcon(HWND owner, UINT callback_message, TrayShellApi shell)
    : owner_(owner), callback_message_(callback_message),
      taskbar_created_message_(RegisterWindowMessageW(L"TaskbarCreated")),
      shell_(shell.add && shell.update && shell.remove
          ? std::move(shell) : native_shell(owner, callback_message)) {}

TrayIcon::~TrayIcon() { remove(); }

bool TrayIcon::install() {
    if (installed_) return shell_.update(state_);
    installed_ = shell_.add();
    if (!installed_) return false;
    if (shell_.update(state_)) return true;
    static_cast<void>(shell_.remove());
    installed_ = false;
    return false;
}

void TrayIcon::update_count(std::uint32_t pending_count) {
    state_.pending_count = pending_count;
    if (installed_) static_cast<void>(shell_.update(state_));
}

void TrayIcon::update_interaction_state(bool enabled) {
    state_.interaction_enabled = enabled;
    if (installed_) static_cast<void>(shell_.update(state_));
}

void TrayIcon::update_state(TrayMenuState state) {
    state_ = state;
    if (installed_) static_cast<void>(shell_.update(state_));
}

bool TrayIcon::show_balloon(std::wstring_view title, std::wstring_view body) {
    if (!installed_) return false;
    auto data = notification_data(owner_, callback_message_, state_, NIF_INFO);
    wcsncpy_s(data.szInfoTitle, std::wstring{title}.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, std::wstring{body}.c_str(), _TRUNCATE);
    data.dwInfoFlags = NIIF_INFO;
    data.uTimeout = 10'000;
    return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

void TrayIcon::remove() noexcept {
    if (installed_) static_cast<void>(shell_.remove());
    installed_ = false;
}

bool TrayIcon::handle_taskbar_created() {
    installed_ = false;
    return install();
}

bool TrayIcon::installed() const noexcept { return installed_; }

void TrayIcon::show_context_menu() {
    if (owner_ == nullptr) return;
    const auto items = build_tray_menu(state_);
    const auto menu = CreatePopupMenu();
    if (menu == nullptr) return;
    for (const auto& item : items) {
        if (item.separator_before) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        UINT flags = MF_STRING;
        if (item.checked) flags |= MF_CHECKED;
        if (!item.enabled) flags |= MF_GRAYED;
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(item.command), item.label.c_str());
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_);
    const auto command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        cursor.x, cursor.y, 0, owner_, nullptr);
    if (command != 0) PostMessageW(owner_, WM_COMMAND, command, 0);
    DestroyMenu(menu);
    PostMessageW(owner_, WM_NULL, 0, 0);
}

UINT TrayIcon::taskbar_created_message() const noexcept {
    return taskbar_created_message_;
}

}  // namespace desktop_todo
