#include "platform/windows/hotkey_service.h"

#include "tests/test_support.h"

#include <map>
#include <string>
#include <vector>

using namespace desktop_todo;

TEST_CASE(hotkey_service_parses_default_show_hide_chord) {
    const auto chord = parse_hotkey(L"Ctrl+Alt+T");

    EXPECT_TRUE(chord.has_value());
    EXPECT_EQ(chord->modifiers, static_cast<UINT>(MOD_CONTROL | MOD_ALT));
    EXPECT_EQ(chord->virtual_key, static_cast<UINT>('T'));
}

TEST_CASE(hotkey_service_reports_conflict_without_losing_previous_registration) {
    std::map<int, HotkeyChord> registered;
    std::vector<int> removed;
    int next_register_error = ERROR_SUCCESS;
    HotkeyService service{
        [&registered, &next_register_error](int id, HotkeyChord chord, DWORD& error) {
            if (next_register_error != ERROR_SUCCESS) {
                error = static_cast<DWORD>(next_register_error);
                next_register_error = ERROR_SUCCESS;
                return false;
            }
            registered[id] = chord;
            error = ERROR_SUCCESS;
            return true;
        },
        [&registered, &removed](int id, DWORD& error) {
            removed.push_back(id);
            error = ERROR_SUCCESS;
            return registered.erase(id) == 1;
        }};

    const auto original = parse_hotkey(L"Ctrl+Alt+T");
    const auto replacement = parse_hotkey(L"Ctrl+Shift+T");
    EXPECT_TRUE(original.has_value() && replacement.has_value());
    EXPECT_TRUE(service.replace(HotkeyAction::show_hide, *original).success);
    const auto original_id = service.registration_id(HotkeyAction::show_hide);
    EXPECT_TRUE(original_id.has_value());

    next_register_error = ERROR_HOTKEY_ALREADY_REGISTERED;
    const auto failed = service.replace(HotkeyAction::show_hide, *replacement);

    EXPECT_TRUE(!failed.success);
    EXPECT_EQ(failed.error, static_cast<DWORD>(ERROR_HOTKEY_ALREADY_REGISTERED));
    EXPECT_EQ(service.registration_id(HotkeyAction::show_hide), original_id);
    EXPECT_EQ(service.chord(HotkeyAction::show_hide), original);
    EXPECT_TRUE(registered.contains(*original_id));
    EXPECT_EQ(removed.size(), static_cast<std::size_t>(0));
}

TEST_CASE(hotkey_service_successfully_replaces_registration_and_routes_new_id) {
    std::map<int, HotkeyChord> registered;
    HotkeyService service{
        [&registered](int id, HotkeyChord chord, DWORD& error) {
            registered[id] = chord;
            error = ERROR_SUCCESS;
            return true;
        },
        [&registered](int id, DWORD& error) {
            error = ERROR_SUCCESS;
            return registered.erase(id) == 1;
        }};
    const auto old_chord = parse_hotkey(L"Ctrl+Alt+T");
    const auto new_chord = parse_hotkey(L"Ctrl+Shift+T");
    EXPECT_TRUE(old_chord.has_value() && new_chord.has_value());
    EXPECT_TRUE(service.replace(HotkeyAction::show_hide, *old_chord).success);
    const auto old_id = service.registration_id(HotkeyAction::show_hide);

    EXPECT_TRUE(service.replace(HotkeyAction::show_hide, *new_chord).success);

    const auto new_id = service.registration_id(HotkeyAction::show_hide);
    EXPECT_TRUE(new_id.has_value() && old_id.has_value() && *new_id != *old_id);
    EXPECT_TRUE(!registered.contains(*old_id));
    EXPECT_EQ(service.action_for(*new_id), HotkeyAction::show_hide);
    EXPECT_EQ(service.chord(HotkeyAction::show_hide), new_chord);
}

