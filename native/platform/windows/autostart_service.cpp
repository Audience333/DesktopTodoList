#include "platform/windows/autostart_service.h"

#include <string>
#include <vector>

namespace desktop_todo {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

DWORD write_hkcu_run(std::wstring_view name, std::wstring_view command) {
    HKEY key = nullptr;
    auto status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return status;
    const std::wstring value_name{name};
    const std::wstring value{command};
    status = RegSetValueExW(key, value_name.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status;
}

DWORD remove_hkcu_run(std::wstring_view name) {
    HKEY key = nullptr;
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (opened != ERROR_SUCCESS) return opened;
    const std::wstring value_name{name};
    const auto removed = RegDeleteValueW(key, value_name.c_str());
    RegCloseKey(key);
    return removed == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : removed;
}

std::wstring registry_value_name() {
#ifdef DESKTOP_TODO_TEST_PROFILE
    constexpr wchar_t prefix[] = L"DesktopTodoListTest_";
    const auto length = GetEnvironmentVariableW(L"DESKTOP_TODO_TEST_AUTOSTART_NAME", nullptr, 0);
    if (length < std::size(prefix) || length > 256) return {};
    std::vector<wchar_t> value(length);
    const auto copied = GetEnvironmentVariableW(
        L"DESKTOP_TODO_TEST_AUTOSTART_NAME", value.data(), length);
    if (copied == 0 || copied >= length) return {};
    std::wstring name{value.data(), copied};
    if (name.compare(0, std::size(prefix) - 1, prefix) != 0) return {};
    return name;
#else
    return std::wstring{AutostartService::value_name()};
#endif
}

}  // namespace

AutostartService::AutostartService(AutostartApi api) : api_(std::move(api)) {
    if (!api_.write_value) api_.write_value = write_hkcu_run;
    if (!api_.remove_value) api_.remove_value = remove_hkcu_run;
}

AutostartResult AutostartService::set_enabled(
    bool enabled, const std::filesystem::path& executable) const {
    const auto name = registry_value_name();
    if (name.empty()) return {false, ERROR_INVALID_PARAMETER};
    DWORD status = ERROR_SUCCESS;
    if (enabled) {
        if (executable.empty()) return {false, ERROR_INVALID_PARAMETER};
        const auto command = L"\"" + executable.wstring() + L"\"";
        status = api_.write_value(name, command);
    } else {
        status = api_.remove_value(name);
    }
    return {status == ERROR_SUCCESS, status};
}

}  // namespace desktop_todo
