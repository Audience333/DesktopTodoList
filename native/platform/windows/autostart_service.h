#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string_view>

namespace desktop_todo {

struct AutostartResult {
    bool success = false;
    DWORD error = ERROR_SUCCESS;
};

struct AutostartApi {
    std::function<DWORD(std::wstring_view, std::wstring_view)> write_value;
    std::function<DWORD(std::wstring_view)> remove_value;
};

class AutostartService {
public:
    explicit AutostartService(AutostartApi api = {});

    [[nodiscard]] static constexpr std::wstring_view value_name() noexcept {
        return L"DesktopTodoList";
    }
    [[nodiscard]] AutostartResult set_enabled(
        bool enabled, const std::filesystem::path& executable) const;

private:
    AutostartApi api_;
};

}  // namespace desktop_todo
