#include "platform/windows/single_instance.h"

#include <sddl.h>

#include <algorithm>
#include <cwctype>
#include <vector>

namespace desktop_todo {
namespace {

bool has_json_extension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".json";
}

}  // namespace

InstanceNames current_user_instance_names() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD byte_count = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &byte_count);
    std::vector<std::byte> buffer(byte_count);
    if (byte_count == 0 || !GetTokenInformation(
            token, TokenUser, buffer.data(), byte_count, &byte_count)) {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);

    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    PWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid_text)) return {};
    const std::wstring sid{sid_text};
    LocalFree(sid_text);
    return {
        L"Global\\DesktopTodoList.SingleInstance.v2." + sid,
        std::wstring{kMessageWindowClass} + L"." + sid};
}

std::wstring encode_launch_request(const LaunchRequest& request) {
    if (request.command == LaunchCommand::show) return L"v1|show|";
    return L"v1|import|" + request.import_path.wstring();
}

std::optional<LaunchRequest> decode_launch_request(std::wstring_view payload) {
    if (payload.empty() || (payload.size() + 1) * sizeof(wchar_t) > kMaxLaunchPayloadBytes) {
        return std::nullopt;
    }
    constexpr std::wstring_view show = L"v1|show|";
    constexpr std::wstring_view import = L"v1|import|";
    if (payload == show) return LaunchRequest{};
    if (!payload.starts_with(import)) return std::nullopt;
    const std::filesystem::path path{payload.substr(import.size())};
    if (path.empty() || !path.is_absolute() || !has_json_extension(path)) return std::nullopt;
    return LaunchRequest{LaunchCommand::import_file, path.lexically_normal()};
}

std::optional<LaunchRequest> make_import_request(const std::filesystem::path& path) {
    if (path.empty() || !has_json_extension(path)) return std::nullopt;
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) return std::nullopt;
    absolute = absolute.lexically_normal();
    const LaunchRequest request{LaunchCommand::import_file, std::move(absolute)};
    const auto encoded = encode_launch_request(request);
    return (encoded.size() + 1) * sizeof(wchar_t) <= kMaxLaunchPayloadBytes
        ? std::optional<LaunchRequest>{request}
        : std::nullopt;
}

SingleInstance::SingleInstance(std::wstring mutex_name, std::wstring message_window_class)
    : mutex_name_(std::move(mutex_name)),
      message_window_class_(std::move(message_window_class)) {
    if (mutex_name_.empty() || message_window_class_.empty()) {
        auto names = current_user_instance_names();
        if (mutex_name_.empty()) mutex_name_ = std::move(names.mutex);
        if (message_window_class_.empty()) {
            message_window_class_ = std::move(names.message_window_class);
        }
    }
}

SingleInstance::~SingleInstance() {
    if (mutex_ != nullptr) CloseHandle(mutex_);
}

AcquireResult SingleInstance::acquire() {
    if (result_.has_value()) return *result_;
    if (mutex_name_.empty() || message_window_class_.empty()) {
        result_ = AcquireResult{AcquireStatus::error, ERROR_INVALID_SID};
        return *result_;
    }
    SetLastError(ERROR_SUCCESS);
    mutex_ = CreateMutexW(nullptr, FALSE, mutex_name_.c_str());
    if (mutex_ == nullptr) {
        result_ = AcquireResult{AcquireStatus::error, GetLastError()};
    } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        result_ = AcquireResult{AcquireStatus::secondary, ERROR_SUCCESS};
    } else {
        result_ = AcquireResult{AcquireStatus::primary, ERROR_SUCCESS};
    }
    return *result_;
}

const std::wstring& SingleInstance::message_window_class_name() const noexcept {
    return message_window_class_;
}

bool SingleInstance::signal_existing(const LaunchRequest& request) const {
    const auto payload = encode_launch_request(request);
    const auto byte_count = (payload.size() + 1) * sizeof(wchar_t);
    if (byte_count > kMaxLaunchPayloadBytes) return false;

    HWND window = nullptr;
    for (int attempt = 0; attempt < 20 && window == nullptr; ++attempt) {
        window = FindWindowExW(
            HWND_MESSAGE, nullptr, message_window_class_.c_str(), nullptr);
        if (window == nullptr) Sleep(50);
    }
    if (window == nullptr) return false;

    DWORD target_process = 0;
    GetWindowThreadProcessId(window, &target_process);
    if (target_process != 0) {
        static_cast<void>(AllowSetForegroundWindow(target_process));
    }

    COPYDATASTRUCT data{};
    data.dwData = kLaunchCopyDataId;
    data.cbData = static_cast<DWORD>(byte_count);
    data.lpData = const_cast<wchar_t*>(payload.c_str());
    DWORD_PTR response = 0;
    return SendMessageTimeoutW(window, WM_COPYDATA, 0,
        reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &response) != 0 &&
        response != 0;
}

}  // namespace desktop_todo
