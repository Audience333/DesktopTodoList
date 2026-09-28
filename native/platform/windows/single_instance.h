#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace desktop_todo {

inline constexpr wchar_t kMessageWindowClass[] = L"DesktopTodoList.MessageWindow.v2";
inline constexpr ULONG_PTR kLaunchCopyDataId = 0x44544C32;
inline constexpr std::size_t kMaxLaunchPayloadBytes = 32 * 1024;

enum class LaunchCommand { show, import_file };

struct LaunchRequest {
    LaunchCommand command = LaunchCommand::show;
    std::filesystem::path import_path;
};

enum class AcquireStatus { primary, secondary, error };

struct AcquireResult {
    AcquireStatus status = AcquireStatus::error;
    DWORD error = ERROR_SUCCESS;
};

struct InstanceNames {
    std::wstring mutex;
    std::wstring message_window_class;
};

[[nodiscard]] std::wstring encode_launch_request(const LaunchRequest& request);
[[nodiscard]] std::optional<LaunchRequest> decode_launch_request(std::wstring_view payload);
[[nodiscard]] std::optional<LaunchRequest> make_import_request(
    const std::filesystem::path& path);
[[nodiscard]] InstanceNames current_user_instance_names();

class SingleInstance {
public:
    explicit SingleInstance(
        std::wstring mutex_name = {},
        std::wstring message_window_class = {});
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    [[nodiscard]] AcquireResult acquire();
    [[nodiscard]] bool signal_existing(const LaunchRequest& request) const;
    [[nodiscard]] const std::wstring& message_window_class_name() const noexcept;

private:
    std::wstring mutex_name_;
    std::wstring message_window_class_;
    HANDLE mutex_ = nullptr;
    std::optional<AcquireResult> result_;
};

}  // namespace desktop_todo
