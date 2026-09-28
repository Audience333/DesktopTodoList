#include "app/application.h"

#include "application/app_service.h"
#include "persistence/file_system.h"
#include "persistence/state_repository.h"
#include "platform/windows/single_instance.h"
#include "platform/windows/window_class.h"
#include "presentation/widget_window.h"

#include <shellapi.h>
#include <shlobj.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace desktop_todo {
namespace {

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

        if (!service_.start()) {
            cleanup_windows();
            return 7;
        }

        widget_ = std::make_unique<WidgetWindow>(service_);
        if (!widget_->create(instance, show_command)) {
            cleanup_windows();
            return 6;
        }
        handle_launch_request(launch_request);

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

    static LRESULT CALLBACK message_window_proc(
        HWND window, UINT message, WPARAM parameter, LPARAM data) {
        auto* self = from_window(window, message, data);
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
            self->handle_launch_request(*request);
            return TRUE;
        }
        return DefWindowProcW(window, message, parameter, data);
    }

    void handle_launch_request(const LaunchRequest& request) {
        if (request.command == LaunchCommand::import_file) {
            pending_import_ = request.import_path;
        }
        if (widget_) widget_->show_and_activate();
    }

    void cleanup_windows() {
        widget_.reset();
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
    std::unique_ptr<WidgetWindow> widget_;
    std::optional<std::filesystem::path> pending_import_;
};

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() = default;

int Application::run(HINSTANCE instance, int show_command) {
    return impl_->run(instance, show_command);
}

}  // namespace desktop_todo
