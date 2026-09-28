#include "test_support.h"

#include "platform/windows/single_instance.h"

#include <filesystem>
#include <string>

namespace {

using desktop_todo::AcquireStatus;
using desktop_todo::LaunchCommand;
using desktop_todo::LaunchRequest;
using desktop_todo::SingleInstance;

struct MessageCapture {
    bool received = false;
    LaunchRequest request;
};

LRESULT CALLBACK capture_proc(HWND window, UINT message, WPARAM parameter, LPARAM data) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(data);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_COPYDATA) {
        auto* capture = reinterpret_cast<MessageCapture*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(data);
        if (capture == nullptr || copy == nullptr ||
            copy->dwData != desktop_todo::kLaunchCopyDataId || copy->lpData == nullptr ||
            copy->cbData < sizeof(wchar_t) || copy->cbData % sizeof(wchar_t) != 0) {
            return FALSE;
        }
        const auto count = copy->cbData / sizeof(wchar_t);
        const auto* text = static_cast<const wchar_t*>(copy->lpData);
        if (text[count - 1] != L'\0') return FALSE;
        const auto decoded = desktop_todo::decode_launch_request({text, count - 1});
        if (!decoded.has_value()) return FALSE;
        capture->received = true;
        capture->request = *decoded;
        return TRUE;
    }
    return DefWindowProcW(window, message, parameter, data);
}

std::wstring unique_name(std::wstring_view suffix) {
    return L"DesktopTodoList.Test." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::wstring{suffix};
}

}  // namespace

TEST_CASE(single_instance_first_owner_and_second_instance_are_distinguished) {
    SingleInstance first{unique_name(L"mutex")};
    SingleInstance second{unique_name(L"mutex")};

    EXPECT_EQ(first.acquire().status, AcquireStatus::primary);
    EXPECT_EQ(second.acquire().status, AcquireStatus::secondary);
}

TEST_CASE(single_instance_default_names_are_scoped_to_current_user_across_sessions) {
    const auto names = desktop_todo::current_user_instance_names();
    EXPECT_TRUE(names.mutex.starts_with(L"Global\\DesktopTodoList.SingleInstance.v2.S-"));
    EXPECT_TRUE(names.message_window_class.starts_with(
        L"DesktopTodoList.MessageWindow.v2.S-"));
}

TEST_CASE(single_instance_protocol_round_trips_show_request) {
    const auto encoded = desktop_todo::encode_launch_request({LaunchCommand::show, {}});
    const auto decoded = desktop_todo::decode_launch_request(encoded);

    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, LaunchCommand::show);
    EXPECT_TRUE(decoded->import_path.empty());
}

TEST_CASE(single_instance_protocol_canonicalizes_json_import_path) {
    const auto request = desktop_todo::make_import_request(L".\\fixtures\\tasks.json");
    EXPECT_TRUE(request.has_value());
    EXPECT_TRUE(request->import_path.is_absolute());
    EXPECT_EQ(request->import_path.extension(), std::filesystem::path{L".json"});

    const auto decoded = desktop_todo::decode_launch_request(
        desktop_todo::encode_launch_request(*request));
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->command, LaunchCommand::import_file);
    EXPECT_EQ(decoded->import_path, request->import_path);
}

TEST_CASE(single_instance_protocol_rejects_malformed_unknown_and_oversized_messages) {
    EXPECT_TRUE(!desktop_todo::decode_launch_request(L"").has_value());
    EXPECT_TRUE(!desktop_todo::decode_launch_request(L"v1|delete|anything").has_value());
    EXPECT_TRUE(!desktop_todo::decode_launch_request(L"v2|show|").has_value());
    EXPECT_TRUE(!desktop_todo::decode_launch_request(
        L"v1|import|" + std::wstring(33 * 1024, L'x')).has_value());
    EXPECT_TRUE(!desktop_todo::make_import_request(L"tasks.txt").has_value());
}

TEST_CASE(single_instance_signal_existing_reaches_message_only_window) {
    const auto class_name = unique_name(L"message-window");
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = capture_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name.c_str();
    EXPECT_TRUE(RegisterClassW(&window_class) != 0);
    MessageCapture capture;
    const auto window = CreateWindowExW(0, class_name.c_str(), nullptr, 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, window_class.hInstance, &capture);
    EXPECT_TRUE(window != nullptr);

    SingleInstance sender{unique_name(L"signal-mutex"), class_name};
    EXPECT_TRUE(sender.signal_existing({LaunchCommand::show, {}}));
    EXPECT_TRUE(capture.received);
    EXPECT_EQ(capture.request.command, LaunchCommand::show);

    DestroyWindow(window);
    UnregisterClassW(class_name.c_str(), window_class.hInstance);
}
