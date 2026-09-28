#include "presentation/settings_panel.h"

#include "tests/test_support.h"

#include <string>
#include <vector>

using namespace desktop_todo;

namespace {

struct SettingsApiFixture {
    std::vector<std::pair<HotkeyAction, std::wstring>> hotkeys;
    bool fail_recovery_hotkey = false;
    bool fail_layer = false;
    bool fail_click_through = false;
    bool saved = false;
    bool tray = true;

    SettingsApplyApi api() {
        return {
            [this](HotkeyAction action, std::wstring_view chord) {
                if (action == HotkeyAction::toggle_interaction && fail_recovery_hotkey &&
                    chord != L"Ctrl+Alt+L") {
                    return HotkeyReplaceResult{false, ERROR_HOTKEY_ALREADY_REGISTERED};
                }
                hotkeys.emplace_back(action, chord);
                return HotkeyReplaceResult{true, ERROR_SUCCESS};
            },
            [this](WindowLayer) { return !fail_layer; },
            [this](bool) { return !fail_click_through; },
            [this] { return tray; },
            [this](const Settings&) { saved = true; return true; }};
    }
};

}  // namespace

TEST_CASE(settings_panel_validates_complete_settings_draft) {
    Settings draft;
    draft.theme = Theme::dark;
    draft.default_filter = ViewKind::week;
    draft.week_starts_on = 0;
    draft.close_to_tray = false;
    draft.hotkey = L"Ctrl+Shift+T";
    draft.selectable_hotkey = L"Ctrl+Alt+L";
    draft.window_layer = WindowLayer::bottom;
    draft.click_through_timeout_minutes = 30;
    draft.multi_select_enabled = false;
    draft.rubber_band_select = false;

    EXPECT_TRUE(validate_settings_draft(draft).empty());
}

TEST_CASE(settings_panel_rejects_invalid_and_conflicting_hotkeys) {
    Settings draft;
    draft.hotkey = L"Ctrl+Alt+T";
    draft.selectable_hotkey = L"Ctrl+Alt+T";
    EXPECT_TRUE(!validate_settings_draft(draft).empty());

    draft.selectable_hotkey = L"not a chord";
    EXPECT_TRUE(!validate_settings_draft(draft).empty());
    draft.selectable_hotkey = L"Ctrl+Alt+L";
    draft.click_through_timeout_minutes = 20;
    EXPECT_TRUE(!validate_settings_draft(draft).empty());
}

TEST_CASE(settings_panel_rolls_back_hotkey_changes_when_draft_commit_fails) {
    Settings current;
    Settings draft = current;
    draft.hotkey = L"Ctrl+Shift+T";
    draft.selectable_hotkey = L"Ctrl+Shift+L";
    SettingsApiFixture fixture;
    fixture.fail_recovery_hotkey = true;

    const auto result = commit_settings_draft(current, draft, fixture.api());

    EXPECT_TRUE(!result.success);
    EXPECT_TRUE(result.rollback_complete);
    EXPECT_TRUE(!fixture.saved);
    EXPECT_EQ(fixture.hotkeys.size(), std::size_t{2});
    EXPECT_EQ(fixture.hotkeys.front().second, L"Ctrl+Shift+T");
    EXPECT_EQ(fixture.hotkeys.back().second, current.hotkey);
}

TEST_CASE(settings_panel_refuses_click_through_without_tray_and_recovery_hotkey) {
    Settings current;
    Settings draft = current;
    draft.selectable = false;
    SettingsApiFixture fixture;
    fixture.tray = false;

    const auto result = commit_settings_draft(current, draft, fixture.api());

    EXPECT_TRUE(!result.success);
    EXPECT_TRUE(!fixture.saved);
}

TEST_CASE(settings_panel_commits_theme_view_close_layer_and_selection_settings) {
    Settings current;
    Settings draft = current;
    draft.theme = Theme::light;
    draft.default_filter = ViewKind::done;
    draft.close_to_tray = false;
    draft.window_layer = WindowLayer::top;
    draft.multi_select_enabled = false;
    draft.rubber_band_select = false;
    SettingsApiFixture fixture;

    const auto result = commit_settings_draft(current, draft, fixture.api());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(fixture.saved);
}
