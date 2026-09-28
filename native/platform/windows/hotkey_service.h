#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace desktop_todo {

enum class HotkeyAction { show_hide, toggle_interaction };

struct HotkeyChord {
    UINT modifiers = 0;
    UINT virtual_key = 0;
    friend bool operator==(const HotkeyChord&, const HotkeyChord&) = default;
};

[[nodiscard]] std::optional<HotkeyChord> parse_hotkey(std::wstring_view text);

struct HotkeyReplaceResult {
    bool success = false;
    DWORD error = ERROR_SUCCESS;
};

class HotkeyService {
public:
    using Register = std::function<bool(int, HotkeyChord, DWORD&)>;
    using Unregister = std::function<bool(int, DWORD&)>;

    HotkeyService(Register register_hotkey, Unregister unregister_hotkey);
    explicit HotkeyService(HWND owner);
    ~HotkeyService();
    HotkeyService(const HotkeyService&) = delete;
    HotkeyService& operator=(const HotkeyService&) = delete;

    [[nodiscard]] HotkeyReplaceResult replace(HotkeyAction action, HotkeyChord chord);
    [[nodiscard]] HotkeyReplaceResult replace(
        HotkeyAction action, std::wstring_view chord_text);
    [[nodiscard]] std::optional<HotkeyAction> action_for(int registration_id) const;
    [[nodiscard]] std::optional<int> registration_id(HotkeyAction action) const;
    [[nodiscard]] std::optional<HotkeyChord> chord(HotkeyAction action) const;

private:
    struct Registration { int id; HotkeyChord chord; };
    Register register_;
    Unregister unregister_;
    int next_id_ = 0xD700;
    std::unordered_map<HotkeyAction, Registration> registrations_;
};

}  // namespace desktop_todo
