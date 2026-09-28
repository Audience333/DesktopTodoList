#include "app/application.h"

#include "application/app_service.h"
#include "persistence/file_system.h"
#include "persistence/state_repository.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/hotkey_service.h"
#include "platform/windows/tray_icon.h"
#include "platform/windows/window_behavior.h"
#include "platform/windows/window_class.h"
#include "presentation/data_transfer_dialog.h"
#include "presentation/settings_panel.h"
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
        widget_->set_file_drop_handler([this](const std::filesystem::path& path) {
            import_file(path, widget_ ? widget_->handle() : nullptr);
        });
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
        const auto timeout = service_.snapshot().settings.click_through_timeout_minutes;
        window_behavior_->set_auto_restore(timeout == 30
            ? std::optional{std::chrono::minutes{30}} : std::nullopt);
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
            break;
        case TrayCommand::settings:
            show_settings();
            break;
        }
        refresh_tray_state();
    }

    SettingsApplyApi settings_api(bool persist) {
        return {
            [this](HotkeyAction action, std::wstring_view chord) {
                return hotkeys_ ? hotkeys_->replace(action, chord)
                    : HotkeyReplaceResult{false, ERROR_INVALID_HANDLE};
            },
            [this](WindowLayer layer) {
                return window_behavior_ && window_behavior_->set_layer(layer);
            },
            [this](bool enabled) {
                return window_behavior_ && window_behavior_->set_click_through(enabled);
            },
            [this] {
                return tray_ && tray_->installed() && hotkeys_ &&
                    hotkeys_->registration_id(HotkeyAction::toggle_interaction).has_value();
            },
            [this, persist](const Settings& settings) {
                if (persist && !service_.update_settings(settings)) return false;
                if (window_behavior_) {
                    window_behavior_->set_auto_restore(settings.click_through_timeout_minutes == 30
                        ? std::optional{std::chrono::minutes{30}} : std::nullopt);
                }
                return true;
            }};
    }

    SettingsApplyResult apply_settings(
        const Settings& current, const Settings& draft, bool persist) {
        auto result = commit_settings_draft(current, draft, settings_api(persist));
        if (result.success && persist) {
            if (widget_) widget_->apply_settings(draft);
            refresh_tray_state();
        }
        return result;
    }

    void show_settings() {
        if (!widget_) return;
        SettingsPanel panel;
        const auto current = service_.snapshot().settings;
        static_cast<void>(panel.show_modal(widget_->handle(), current,
            [this](const Settings& previous, const Settings& draft) {
                const auto result = apply_settings(previous, draft, true);
                if (!result.success && !result.rollback_complete)
                    MessageBoxW(widget_ ? widget_->handle() : nullptr,
                        L"设置失败，部分系统状态未能自动恢复。请立即检查窗口交互状态和托盘图标。",
                        L"DesktopTodoList", MB_OK | MB_ICONERROR);
                return result;
            }, [this](HWND owner, SettingsTransferAction action) {
                return handle_data_transfer(owner, action);
            }));
    }

    void show_transfer_error(HWND owner, std::wstring_view message) {
        std::wstring text{message};
        MessageBoxW(owner, text.c_str(), L"DesktopTodoList", MB_OK | MB_ICONWARNING);
    }

    bool import_file(const std::filesystem::path& path, HWND owner) {
        std::wstring error;
        auto bytes = DataTransferDialog::read_json_file(path, error);
        if (!bytes.has_value()) {
            show_transfer_error(owner, error);
            return false;
        }
        const auto choice = MessageBoxW(owner,
            L"选择导入方式：\n“是”合并任务，不覆盖现有任务；\n“否”替换全部数据（将先备份当前数据）。",
            L"导入 JSON", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL) return false;
        const auto mode = choice == IDYES ? ImportMode::merge : ImportMode::replace;
        if (mode == ImportMode::replace && MessageBoxW(owner,
                L"替换会覆盖当前全部任务和设置。应用会先创建备份，确定继续吗？",
                L"再次确认替换", MB_YESNO | MB_ICONWARNING) != IDYES) return false;

        auto imported = service_.prepare_import(*bytes, mode);
        if (!imported.candidate.has_value()) {
            show_transfer_error(owner, imported.error.empty() ? L"JSON 文件内容无效。" : imported.error);
            return false;
        }
        const auto count = imported.added;
        const auto issues = imported.issues.size();
        const auto old_settings = service_.snapshot().settings;
        const auto imported_settings = imported.candidate->settings;
        if (mode == ImportMode::replace) {
            const auto applied = apply_settings(old_settings, imported_settings, false);
            if (!applied.success) {
                auto message = applied.error;
                if (!applied.rollback_complete) message += L" 系统状态未能完全恢复，请立即检查托盘与快捷键。";
                show_transfer_error(owner, message);
                return false;
            }
        }
        if (!service_.accept_import(std::move(imported))) {
            if (mode == ImportMode::replace) {
                const auto restored = apply_settings(imported_settings, old_settings, false);
                if (!restored.rollback_complete)
                    show_transfer_error(owner, L"导入备份失败，且窗口系统状态未能完全恢复。请检查托盘与快捷键。");
            }
            show_transfer_error(owner, L"导入没有完成；原有数据仍已保留。请检查磁盘空间和写入权限。 ");
            return false;
        }
        if (mode == ImportMode::replace && widget_) widget_->apply_settings(imported_settings);
        std::wstring summary = mode == ImportMode::merge
            ? L"合并完成，新增任务 " : L"替换完成，导入任务 ";
        summary += std::to_wstring(count) + L" 项。";
        if (issues != 0) summary += L"另有 " + std::to_wstring(issues) + L" 项数据已自动修复。";
        MessageBoxW(owner, summary.c_str(), L"导入完成", MB_OK | MB_ICONINFORMATION);
        refresh_tray_state();
        return true;
    }

    std::optional<Settings> handle_data_transfer(HWND owner, SettingsTransferAction action) {
        if (action == SettingsTransferAction::import_json) {
            const auto path = DataTransferDialog::choose_import_file(owner);
            if (path && import_file(*path, owner)) return service_.snapshot().settings;
            return std::nullopt;
        }
        if (action == SettingsTransferAction::export_json) {
            const auto path = DataTransferDialog::choose_export_file(owner);
            if (!path) return std::nullopt;
            auto destination = *path;
            if (destination.extension().empty()) destination += L".json";
            if (!DataTransferDialog::accepts_json_path(destination)) {
                show_transfer_error(owner, L"导出文件扩展名必须为 .json。");
                return std::nullopt;
            }
            const auto result = DataTransferDialog::export_to(service_, destination);
            if (!result.ok) show_transfer_error(owner, result.error);
            else MessageBoxW(owner, L"数据已成功导出。", L"导出完成", MB_OK | MB_ICONINFORMATION);
            return std::nullopt;
        }
        if (MessageBoxW(owner,
                L"此操作会先备份当前数据，再清空任务并恢复默认设置。确定继续吗？",
                L"重置数据", MB_YESNO | MB_ICONWARNING) != IDYES) return std::nullopt;
        if (MessageBoxW(owner,
                L"最后确认：当前任务与设置将被重置。只有完成备份后才会执行。继续吗？",
                L"再次确认重置", MB_YESNO | MB_ICONWARNING) != IDYES) return std::nullopt;

        const auto previous = service_.snapshot().settings;
        const Settings defaults;
        const auto applied = apply_settings(previous, defaults, false);
        if (!applied.success) {
            show_transfer_error(owner, applied.error);
            return std::nullopt;
        }
        if (!DataTransferDialog::reset_with_confirmation(service_, true, true)) {
            const auto restored = apply_settings(defaults, previous, false);
            if (!restored.rollback_complete)
                show_transfer_error(owner, L"重置失败，且窗口状态未能完全恢复。请检查托盘与快捷键。");
            else show_transfer_error(owner, L"重置失败，原数据保持不变。请检查备份目录的写入权限。");
            return std::nullopt;
        }
        if (widget_) widget_->apply_settings(defaults);
        refresh_tray_state();
        MessageBoxW(owner, L"重置完成；原数据备份已保留。", L"DesktopTodoList",
            MB_OK | MB_ICONINFORMATION);
        return defaults;
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
