#include "platform/windows/tray_icon.h"

#include "tests/test_support.h"

#include <algorithm>

using namespace desktop_todo;

namespace {

const TrayMenuItem& item(const std::vector<TrayMenuItem>& menu, TrayCommand command) {
    const auto found = std::find_if(menu.begin(), menu.end(),
        [command](const TrayMenuItem& value) { return value.command == command; });
    if (found == menu.end()) throw std::runtime_error("tray menu item missing");
    return *found;
}

}  // namespace

TEST_CASE(tray_menu_reflects_visibility_layer_and_interaction_state) {
    const auto menu = build_tray_menu({
        .window_visible = true,
        .pending_count = 3,
        .layer = WindowLayer::top,
        .interaction_enabled = false,
        .close_to_tray = true});

    EXPECT_TRUE(item(menu, TrayCommand::toggle_visibility).checked);
    EXPECT_TRUE(item(menu, TrayCommand::layer_top).checked);
    EXPECT_TRUE(!item(menu, TrayCommand::layer_normal).checked);
    EXPECT_TRUE(!item(menu, TrayCommand::toggle_interaction).checked);
    EXPECT_TRUE(item(menu, TrayCommand::toggle_close_behavior).checked);
    EXPECT_TRUE(item(menu, TrayCommand::settings).enabled);
}

TEST_CASE(tray_menu_and_tooltip_expose_pending_count_and_click_through_state) {
    const auto state = TrayMenuState{
        .window_visible = false,
        .pending_count = 27,
        .layer = WindowLayer::normal,
        .interaction_enabled = false,
        .close_to_tray = false};
    const auto menu = build_tray_menu(state);
    const auto label = item(menu, TrayCommand::pending_count).label;
    const auto tooltip = tray_tooltip(state);

    EXPECT_TRUE(label.find(L"27") != std::wstring::npos);
    EXPECT_TRUE(tooltip.find(L"27") != std::wstring::npos);
    EXPECT_TRUE(tooltip.find(L"鼠标穿透") != std::wstring::npos);
}

TEST_CASE(tray_menu_disables_click_through_when_a_recovery_route_is_unavailable) {
    const auto menu = build_tray_menu({
        .window_visible = true,
        .interaction_toggle_available = false});

    EXPECT_TRUE(!item(menu, TrayCommand::toggle_interaction).enabled);
}

TEST_CASE(tray_icon_reinstalls_after_explorer_taskbar_restart) {
    int adds = 0;
    int updates = 0;
    TrayShellApi shell{
        [&adds] { ++adds; return true; },
        [&updates](const TrayMenuState&) { ++updates; return true; },
        [] { return true; }};
    TrayIcon tray{nullptr, WM_APP + 0x41, std::move(shell)};

    EXPECT_TRUE(tray.install());
    EXPECT_TRUE(tray.handle_taskbar_created());
    EXPECT_EQ(adds, 2);
    EXPECT_TRUE(updates >= 1);
}

TEST_CASE(widget_close_defaults_to_hide_but_explicit_exit_remains_distinct) {
    EXPECT_EQ(close_disposition(true), CloseDisposition::hide_to_tray);
    EXPECT_EQ(close_disposition(false), CloseDisposition::exit_application);
}
