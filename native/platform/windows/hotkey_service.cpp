#include "platform/windows/hotkey_service.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace desktop_todo {
namespace {

std::wstring uppercase(std::wstring_view value) {
    std::wstring result{value};
    std::transform(result.begin(), result.end(), result.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
    return result;
}

std::optional<UINT> key_code(const std::wstring& value) {
    if (value.size() == 1 && ((value[0] >= L'A' && value[0] <= L'Z') ||
        (value[0] >= L'0' && value[0] <= L'9'))) {
        return static_cast<UINT>(value[0]);
    }
    if (value.size() >= 2 && value[0] == L'F') {
        try {
            const auto number = std::stoi(value.substr(1));
            if (number >= 1 && number <= 24 && value == L"F" + std::to_wstring(number))
                return static_cast<UINT>(VK_F1 + number - 1);
        } catch (...) { return std::nullopt; }
    }
    if (value == L"SPACE") return VK_SPACE;
    if (value == L"INSERT") return VK_INSERT;
    if (value == L"DELETE") return VK_DELETE;
    if (value == L"HOME") return VK_HOME;
    if (value == L"END") return VK_END;
    if (value == L"PAGEUP") return VK_PRIOR;
    if (value == L"PAGEDOWN") return VK_NEXT;
    if (value == L"UP") return VK_UP;
    if (value == L"DOWN") return VK_DOWN;
    if (value == L"LEFT") return VK_LEFT;
    if (value == L"RIGHT") return VK_RIGHT;
    return std::nullopt;
}

}  // namespace

std::optional<HotkeyChord> parse_hotkey(std::wstring_view text) {
    UINT modifiers = 0;
    UINT key = 0;
    bool has_modifier = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(L'+', start);
        const auto token = uppercase(text.substr(start,
            end == std::wstring_view::npos ? text.size() - start : end - start));
        if (token.empty()) return std::nullopt;
        if (token == L"CTRL" || token == L"CONTROL") {
            if ((modifiers & MOD_CONTROL) != 0) return std::nullopt;
            modifiers |= MOD_CONTROL;
            has_modifier = true;
        } else if (token == L"ALT") {
            if ((modifiers & MOD_ALT) != 0) return std::nullopt;
            modifiers |= MOD_ALT;
            has_modifier = true;
        } else if (token == L"SHIFT") {
            if ((modifiers & MOD_SHIFT) != 0) return std::nullopt;
            modifiers |= MOD_SHIFT;
            has_modifier = true;
        } else if (token == L"WIN" || token == L"WINDOWS") {
            if ((modifiers & MOD_WIN) != 0) return std::nullopt;
            modifiers |= MOD_WIN;
            has_modifier = true;
        } else {
            if (key != 0) return std::nullopt;
            const auto parsed = key_code(token);
            if (!parsed.has_value()) return std::nullopt;
            key = *parsed;
        }
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    if (!has_modifier || key == 0) return std::nullopt;
    return HotkeyChord{modifiers, key};
}

std::wstring format_hotkey_registration_warning(
    HotkeyAction action, std::wstring_view chord, DWORD error) {
    const auto action_name = action == HotkeyAction::show_hide
        ? L"全局显示快捷键" : L"交互恢复快捷键";
    auto warning = std::wstring{action_name} + L" " + std::wstring{chord} +
        L" 无法注册（Windows 错误码 " + std::to_wstring(error) +
        L"）。请在设置中更换快捷键。";
    if (action == HotkeyAction::show_hide) {
        warning += L"仍可通过通知区域图标显示窗口。";
    } else {
        warning += L"鼠标穿透保持禁用，以免窗口无法找回。";
    }
    return warning;
}

HotkeyService::HotkeyService(Register register_hotkey, Unregister unregister_hotkey)
    : register_(std::move(register_hotkey)), unregister_(std::move(unregister_hotkey)) {}

HotkeyService::HotkeyService(HWND owner)
    : HotkeyService(
        [owner](int id, HotkeyChord chord, DWORD& error) {
            const auto registered = RegisterHotKey(owner, id,
                chord.modifiers | MOD_NOREPEAT, chord.virtual_key);
            error = registered ? ERROR_SUCCESS : GetLastError();
            return registered != FALSE;
        },
        [owner](int id, DWORD& error) {
            const auto removed = UnregisterHotKey(owner, id);
            error = removed ? ERROR_SUCCESS : GetLastError();
            return removed != FALSE;
        }) {}

HotkeyService::~HotkeyService() {
    for (const auto& [action, registration] : registrations_) {
        static_cast<void>(action);
        DWORD error = ERROR_SUCCESS;
        static_cast<void>(unregister_(registration.id, error));
    }
}

HotkeyReplaceResult HotkeyService::replace(HotkeyAction action, HotkeyChord chord_value) {
    const auto current = registrations_.find(action);
    if (current != registrations_.end() && current->second.chord == chord_value)
        return {true, ERROR_SUCCESS};

    const auto new_id = next_id_++;
    DWORD error = ERROR_SUCCESS;
    if (!register_(new_id, chord_value, error)) {
        return {false, error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error};
    }
    if (current != registrations_.end()) {
        DWORD unregister_error = ERROR_SUCCESS;
        if (!unregister_(current->second.id, unregister_error)) {
            DWORD rollback_error = ERROR_SUCCESS;
            static_cast<void>(unregister_(new_id, rollback_error));
            return {false, unregister_error == ERROR_SUCCESS
                ? ERROR_GEN_FAILURE : unregister_error};
        }
        current->second = {new_id, chord_value};
    } else {
        registrations_.emplace(action, Registration{new_id, chord_value});
    }
    return {true, ERROR_SUCCESS};
}

HotkeyReplaceResult HotkeyService::replace(
    HotkeyAction action, std::wstring_view chord_text) {
    const auto parsed = parse_hotkey(chord_text);
    return parsed.has_value()
        ? replace(action, *parsed)
        : HotkeyReplaceResult{false, ERROR_INVALID_PARAMETER};
}

std::optional<HotkeyAction> HotkeyService::action_for(int registration_id) const {
    for (const auto& [action, registration] : registrations_) {
        if (registration.id == registration_id) return action;
    }
    return std::nullopt;
}

std::optional<int> HotkeyService::registration_id(HotkeyAction action) const {
    const auto registration = registrations_.find(action);
    return registration == registrations_.end()
        ? std::nullopt : std::optional<int>{registration->second.id};
}

std::optional<HotkeyChord> HotkeyService::chord(HotkeyAction action) const {
    const auto registration = registrations_.find(action);
    return registration == registrations_.end()
        ? std::nullopt : std::optional<HotkeyChord>{registration->second.chord};
}

}  // namespace desktop_todo
