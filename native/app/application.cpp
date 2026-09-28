#include "app/application.h"

#include "application/app_service.h"
#include "persistence/file_system.h"
#include "persistence/state_repository.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/hotkey_service.h"
#include "platform/windows/tray_icon.h"
#include "platform/windows/window_behavior.h"
#include "platform/windows/window_class.h"
#include "presentation/widget_window.h"

#include <shellapi.h>
#include <shlobj.h>

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace desktop_todo {
namespace {

constexpr UINT kShowExistingWidgetMessage = WM_APP + 0x31;
constexpr UINT kTrayCallbackMessage = WM_APP + 0x41;
constexpr UINT_PTR kWindowBehaviorTimer = 0xD712;

Clock::time_point current_time() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
}

LocalDate current_local_date() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    return {value.wYear, value.wMonth, value.wDay};
}

std::wstring timestamp() {
    SYSTEMTIME value{};
    GetLocalTime(&value);
    wchar_t text[32]{};
    swprintf_s(text, L"%04u%02u%02u-%02u%02u%02u",
        value.wYear, value.wMonth, value.wDay,
        value.wHour, value.wMinute, value.wSecond);
    return text;
}

std::wstring new_id() {
    GUID value{};
    if (FAILED(CoCreateGuid(&value))) {
        return timestamp() + L"-" + std::to_wstring(GetTickCount64());
    }
    wchar_t text[40]{};
    StringFromGUID2(value, text, static_cast<int>(std::size(text)));
    return text;
}

std::filesystem::path data_directory() {
    PWSTR value = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value))) {
        std::filesystem::path result{value};
        CoTaskMemFree(value);
        return result / L"DesktopTodoList";
    }
    return {};
}

LaunchRequest command_line_request() {
    int count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    LaunchRequest request;
    if (arguments != nullptr && count == 2) {
        if (const auto imported = make_import_request(arguments[1]); imported.has_value()) {
            request = *imported;
        }
    }
    if (arguments != nullptr) LocalFree(arguments);
    return request;
}

}  // namespace

class Application::Impl {
public:
    Impl()
        : data_directory_(data_directory()),
          repository_(files_, data_directory_, timestamp),
          service_(repository_, current_time, new_id, current_local_date,
              [this](const AppEvent&) {
                  if (widget_) widget_->invalidate();
                  refresh_tray_state();
              }) {}

    int run(HINSTANCE instance, int show_command) {
        const auto launch_request = command_line_request();
        const auto acquired = single_instance_.acquire();
        if (acquired.status == AcquireStatus::secondary) {
            return single_instance_.signal_existing(launch_request) ? 0 : 3;
        }
        if (acquired.status != AcquireStatus::primary) return 2;
        if (data_directory_.empty()) return 8;

        WindowClass message_class{
            instance, single_instance_.message_window_class_name(),
            &Impl::message_window_proc, nullptr};
        if (!message_class.registered()) return 4;

        message_ = CreateWindowExW(0, message_class.name(), nullptr, 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        if (message_ == nullptr) return 5;
        broadcast_ = CreateWindowExW(0, message_class.name(), nullptr, 0,
            0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (broadcast_ == nullptr) {
            cleanup_windows();
            return 5;
        }

        if (!service_.start()) {
            cleanup_windows();
            return 7;
        }
        widget_ = std::make_unique<WidgetWindow>(service_);
        const auto initial_show_command = service_.snapshot().settings.start_minimized
            ? SW_HIDE : show_command;
        if (!widget_->create(instance, initial_show_command)) {
            cleanup_windows();
            return 6;
        }
        tray_ = std::make_unique<TrayIcon>(message_, kTrayCallbackMessage);
        if (!tray_->install()) {
            MessageBoxW(nullptr, L"无法注册通知区域图标。为避免窗口收起后无法找回，程序已停止启动。",
                L"DesktopTodoList", MB_OK | MB_ICONERROR);
            cleanup_windows();
            return 10;
        }
        hotkeys_ = std::make_unique<HotkeyService>(message_);
        const auto hotkey = parse_hotkey(service_.snapshot().settings.hotkey);
        if (hotkey.has_value()) {
            const auto registered = hotkeys_->replace(HotkeyAction::show_hide, *hotkey);
            if (!registered.success) {
                MessageBoxW(nullptr, L"全局显示快捷键无法注册，可能与其他程序冲突。仍可通过通知区域图标显示窗口。",
                    L"DesktopTodoList", MB_OK | MB_ICONWARNING);
            }
        }
        const auto recovery_hotkey = parse_hotkey(service_.snapshot().settings.selectable_hotkey);
        if (recovery_hotkey.has_value()) {
            const auto registered = hotkeys_->replace(
                HotkeyAction::toggle_interaction, *recovery_hotkey);
            if (!registered.success) {
                MessageBoxW(nullptr, L"交互恢复快捷键无法注册。鼠标穿透选项将保持禁用，以免窗口无法找回。",
                    L"DesktopTodoList", MB_OK | MB_ICONWARNING);
            }
        }
        window_behavior_ = std::make_unique<WindowBehavior>(WindowBehaviorApi{
            [this](WindowLayer previous, WindowLayer target) {
                return apply_window_layer(widget_ ? widget_->handle() : nullptr,
                    target, previous, !service_.snapshot().settings.selectable);
            },
            [this](bool enabled) {
                return widget_ && widget_->set_click_through(enabled);
            },
            [this] { return tray_ && tray_->installed(); },
            [this] {
                return hotkeys_ && hotkeys_->registration_id(
                    HotkeyAction::toggle_interaction).has_value();
            },
            [] { return std::chrono::steady_clock::now(); }});
        window_behavior_->set_auto_restore(std::nullopt);
        if (!window_behavior_->initialize(service_.snapshot().settings.window_layer,
                !service_.snapshot().settings.selectable)) {
            static_cast<void>(service_.set_selectable(true));
            static_cast<void>(service_.set_window_layer(window_behavior_->snapshot().layer));
            MessageBoxW(nullptr, L"已保存的窗口模式无法安全恢复，已改为允许交互。可通过通知区域菜单重新设置。",
                L"DesktopTodoList", MB_OK | MB_ICONWARNING);
        }
        SetTimer(message_, kWindowBehaviorTimer, 1000, nullptr);
        foreground_owner_ = this;
        foreground_hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND, nullptr, &Impl::foreground_event,
            0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        refresh_tray_state();
        if (!service_.snapshot().settings.start_minimized ||
            launch_request.command == LaunchCommand::import_file) {
            handle_launch_request(launch_request);
        }

        MSG message{};
        BOOL message_status = 0;
        while ((message_status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        static_cast<void>(service_.flush());
        cleanup_windows();
        return message_status == -1 ? 9 : static_cast<int>(message.wParam);
    }

private:
    static Impl* from_window(HWND window, UINT message, LPARAM parameter) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(parameter);
            auto* self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return self;
        }
        return reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    static void CALLBACK foreground_event(HWINEVENTHOOK, DWORD, HWND foreground,
        LONG, LONG, DWORD, DWORD) {
        if (foreground_owner_ && foreground_owner_->window_behavior_ &&
            foreground_owner_->widget_ && foreground != foreground_owner_->widget_->handle()) {
            foreground_owner_->window_behavior_->on_foreground_changed();
        }
    }

    static LRESULT CALLBACK message_window_proc(
        HWND window, UINT message, WPARAM parameter, LPARAM data) {
        auto* self = from_window(window, message, data);
        if (message == kShowExistingWidgetMessage && self != nullptr) {
            self->handle_launch_request({LaunchCommand::show, {}});
            return 0;
        }
        if (self != nullptr && self->tray_ &&
            message == self->tray_->taskbar_created_message()) {
            if (!self->tray_->handle_taskbar_created() && self->window_behavior_ &&
                self->window_behavior_->snapshot().click_through) {
                self->window_behavior_->restore_interaction();
                if (!self->window_behavior_->snapshot().click_through) {
                    static_cast<void>(self->service_.set_selectable(true));
                } else {
                    MessageBoxW(nullptr, L"托盘恢复失败，且窗口样式无法恢复交互。请按 Ctrl+Alt+L 重试。",
                        L"DesktopTodoList", MB_OK | MB_ICONERROR);
                }
            }
            self->refresh_tray_state();
            return 0;
        }
        if (message == kTrayCallbackMessage && self != nullptr) {
            self->handle_tray_callback(static_cast<UINT>(parameter), data);
            return 0;
        }
        if (message == WM_HOTKEY && self != nullptr && self->hotkeys_) {
            const auto action = self->hotkeys_->action_for(static_cast<int>(parameter));
            if (action == std::optional<HotkeyAction>{HotkeyAction::show_hide} && self->widget_) {
                static_cast<void>(self->widget_->toggle_visibility());
                self->refresh_tray_state();
            } else if (action == std::optional<HotkeyAction>{HotkeyAction::toggle_interaction} &&
                self->window_behavior_) {
                const auto enable = !self->window_behavior_->snapshot().click_through;
                if (self->window_behavior_->set_click_through(enable)) {
                    static_cast<void>(self->service_.set_selectable(!enable));
                    self->refresh_tray_state();
                }
            }
            return 0;
        }
        if (message == WM_TIMER && self != nullptr && parameter == kWindowBehaviorTimer) {
            if (self->window_behavior_ && self->window_behavior_->poll_timeout()) {
                static_cast<void>(self->service_.set_selectable(true));
                self->refresh_tray_state();
            }
            return 0;
        }
        if (message == WM_COMMAND && self != nullptr) {
            self->handle_tray_command(static_cast<TrayCommand>(LOWORD(parameter)));
            return 0;
        }
        if (message == WM_COPYDATA && self != nullptr) {
            const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(data);
            if (copy == nullptr || copy->dwData != kLaunchCopyDataId || copy->lpData == nullptr ||
                copy->cbData < sizeof(wchar_t) || copy->cbData > kMaxLaunchPayloadBytes ||
                copy->cbData % sizeof(wchar_t) != 0) {
                return FALSE;
            }
            const auto count = copy->cbData / sizeof(wchar_t);
            const auto* text = static_cast<const wchar_t*>(copy->lpData);
            if (text[count - 1] != L'\0' || wcsnlen_s(text, count) != count - 1) return FALSE;
            const auto request = decode_launch_request({text, count - 1});
            if (!request.has_value()) return FALSE;
            return self->queue_launch_request(*request) ? TRUE : FALSE;
        }
        return DefWindowProcW(window, message, parameter, data);
    }

    bool queue_launch_request(const LaunchRequest& request) {
        if (request.command == LaunchCommand::import_file) {
            pending_import_ = request.import_path;
        }
        return PostMessageW(message_, kShowExistingWidgetMessage, 0, 0) != FALSE;
    }

    void refresh_tray_state() {
        if (!tray_ || !widget_) return;
        const auto& state = service_.snapshot();
        const auto pending = static_cast<std::uint32_t>(std::count_if(
            state.tasks.begin(), state.tasks.end(), [](const Task& task) {
                return task.status == TaskStatus::todo;
            }));
        tray_->update_state({
            .window_visible = IsWindowVisible(widget_->handle()) != FALSE,
            .pending_count = pending,
            .layer = state.settings.window_layer,
            .interaction_enabled = state.settings.selectable,
            .interaction_toggle_available = tray_->installed() && hotkeys_ &&
                hotkeys_->registration_id(HotkeyAction::toggle_interaction).has_value(),
            .close_to_tray = state.settings.close_to_tray});
    }

    void handle_tray_callback(UINT, LPARAM event) {
        if (!tray_ || !widget_) return;
        const auto code = LOWORD(event);
        if (code == WM_RBUTTONUP || code == WM_CONTEXTMENU) {
            refresh_tray_state();
            tray_->show_context_menu();
        } else if (code == WM_LBUTTONUP || code == WM_LBUTTONDBLCLK ||
            code == NIN_SELECT || code == NIN_KEYSELECT) {
            static_cast<void>(widget_->toggle_visibility());
            refresh_tray_state();
        }
    }

    void handle_tray_command(TrayCommand command) {
        if (!widget_ || !tray_) return;
        switch (command) {
        case TrayCommand::toggle_visibility:
            static_cast<void>(widget_->toggle_visibility());
            break;
        case TrayCommand::new_task:
            widget_->show_and_activate();
            widget_->begin_new_task();
            break;
        case TrayCommand::layer_top:
        case TrayCommand::layer_normal:
        case TrayCommand::layer_bottom: {
            const auto layer = command == TrayCommand::layer_top ? WindowLayer::top :
                command == TrayCommand::layer_bottom ? WindowLayer::bottom : WindowLayer::normal;
            if (window_behavior_ && window_behavior_->set_layer(layer)) {
                static_cast<void>(service_.set_window_layer(layer));
            } else {
                MessageBeep(MB_ICONWARNING);
            }
            break;
        }
        case TrayCommand::toggle_interaction: {
            const auto enable = window_behavior_ &&
                !window_behavior_->snapshot().click_through;
            if (window_behavior_ && window_behavior_->set_click_through(enable)) {
                static_cast<void>(service_.set_selectable(!enable));
            } else {
                MessageBoxW(nullptr, L"无法安全切换鼠标交互状态。请确认通知区域图标与恢复快捷键均可用。",
                    L"DesktopTodoList", MB_OK | MB_ICONWARNING);
            }
            break;
        }
        case TrayCommand::toggle_close_behavior: {
            const auto enable = !service_.snapshot().settings.close_to_tray;
            static_cast<void>(service_.set_close_to_tray(enable));
            break;
        }
        case TrayCommand::exit_application:
            widget_->destroy();
            break;
        case TrayCommand::pending_count:
        case TrayCommand::settings:
            break;
        }
        refresh_tray_state();
    }

    void handle_launch_request(const LaunchRequest& request) {
        if (request.command == LaunchCommand::import_file) {
            pending_import_ = request.import_path;
        }
        if (widget_) widget_->show_and_activate();
        refresh_tray_state();
    }

    void cleanup_windows() {
        if (foreground_hook_ != nullptr) {
            UnhookWinEvent(foreground_hook_);
            foreground_hook_ = nullptr;
        }
        if (foreground_owner_ == this) foreground_owner_ = nullptr;
        if (message_ != nullptr) KillTimer(message_, kWindowBehaviorTimer);
        window_behavior_.reset();
        hotkeys_.reset();
        if (tray_) tray_->remove();
        tray_.reset();
        widget_.reset();
        if (broadcast_ != nullptr) {
            DestroyWindow(broadcast_);
            broadcast_ = nullptr;
        }
        if (message_ != nullptr) {
            DestroyWindow(message_);
            message_ = nullptr;
        }
    }

    std::filesystem::path data_directory_;
    Win32FileSystem files_;
    StateRepository repository_;
    AppService service_;
    SingleInstance single_instance_;
    HWND message_ = nullptr;
    HWND broadcast_ = nullptr;
    std::unique_ptr<WidgetWindow> widget_;
    std::unique_ptr<TrayIcon> tray_;
    std::unique_ptr<HotkeyService> hotkeys_;
    std::unique_ptr<WindowBehavior> window_behavior_;
    HWINEVENTHOOK foreground_hook_ = nullptr;
    std::optional<std::filesystem::path> pending_import_;
    inline static Impl* foreground_owner_ = nullptr;
};

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() = default;

int Application::run(HINSTANCE instance, int show_command) {
    return impl_->run(instance, show_command);
}

}  // namespace desktop_todo
