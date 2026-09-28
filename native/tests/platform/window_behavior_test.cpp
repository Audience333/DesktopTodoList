#include "platform/windows/window_behavior.h"

#include "tests/test_support.h"

#include <chrono>
#include <optional>
#include <vector>

using namespace desktop_todo;

namespace {

struct BehaviorFixture {
    using Clock = std::chrono::steady_clock;
    Clock::time_point now{};
    bool tray_available = true;
    bool hotkey_available = true;
    bool layer_apply_succeeds = true;
    bool click_through_apply_succeeds = true;
    std::vector<WindowLayer> layers;
    std::vector<bool> click_through_changes;

    WindowBehaviorApi api() {
        return {
            [this](WindowLayer, WindowLayer layer) {
                layers.push_back(layer);
                return layer_apply_succeeds;
            },
            [this](bool enabled) {
                click_through_changes.push_back(enabled);
                return click_through_apply_succeeds;
            },
            [this] { return tray_available; },
            [this] { return hotkey_available; },
            [this] { return now; }};
    }
};

}  // namespace

TEST_CASE(window_behavior_layer_transitions_are_mutually_exclusive_and_transactional) {
    BehaviorFixture fixture;
    WindowBehavior behavior{fixture.api()};

    EXPECT_TRUE(behavior.set_layer(WindowLayer::top));
    EXPECT_EQ(behavior.snapshot().layer, WindowLayer::top);
    EXPECT_TRUE(behavior.set_layer(WindowLayer::bottom));
    EXPECT_EQ(behavior.snapshot().layer, WindowLayer::bottom);
    fixture.layer_apply_succeeds = false;
    EXPECT_TRUE(!behavior.set_layer(WindowLayer::normal));
    EXPECT_EQ(behavior.snapshot().layer, WindowLayer::bottom);
    EXPECT_EQ(fixture.layers.size(), std::size_t{3});
}

TEST_CASE(window_behavior_style_bits_preserve_tool_window_and_restore_layer_rules) {
    const auto original = static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
    const auto click_through = click_through_extended_style(
        original, true, WindowLayer::normal);
    EXPECT_TRUE((click_through & WS_EX_LAYERED) != 0);
    EXPECT_TRUE((click_through & WS_EX_TRANSPARENT) != 0);
    EXPECT_TRUE((click_through & WS_EX_NOACTIVATE) != 0);
    EXPECT_TRUE((click_through & WS_EX_TOOLWINDOW) != 0);

    const auto restored = click_through_extended_style(
        click_through, false, WindowLayer::normal);
    EXPECT_TRUE((restored & WS_EX_LAYERED) == 0);
    EXPECT_TRUE((restored & WS_EX_TRANSPARENT) == 0);
    EXPECT_TRUE((restored & WS_EX_NOACTIVATE) == 0);
    EXPECT_TRUE((restored & WS_EX_TOOLWINDOW) != 0);
    EXPECT_TRUE((layer_extended_style(original, WindowLayer::bottom, false) &
        WS_EX_NOACTIVATE) != 0);
}

TEST_CASE(window_behavior_bottom_mode_is_reasserted_after_foreground_changes) {
    BehaviorFixture fixture;
    WindowBehavior behavior{fixture.api()};

    behavior.on_foreground_changed();
    EXPECT_TRUE(fixture.layers.empty());
    EXPECT_TRUE(behavior.set_layer(WindowLayer::bottom));
    behavior.on_foreground_changed();

    EXPECT_EQ(fixture.layers.size(), std::size_t{2});
    EXPECT_EQ(fixture.layers.back(), WindowLayer::bottom);
}

TEST_CASE(window_behavior_refuses_click_through_without_both_escape_routes) {
    BehaviorFixture fixture;
    WindowBehavior behavior{fixture.api()};
    fixture.tray_available = false;
    EXPECT_TRUE(!behavior.set_click_through(true));
    fixture.tray_available = true;
    fixture.hotkey_available = false;
    EXPECT_TRUE(!behavior.set_click_through(true));

    EXPECT_TRUE(fixture.click_through_changes.empty());
    EXPECT_TRUE(!behavior.snapshot().click_through);
}

TEST_CASE(window_behavior_hotkey_and_tray_restore_interaction) {
    BehaviorFixture fixture;
    WindowBehavior behavior{fixture.api()};

    EXPECT_TRUE(behavior.set_click_through(true));
    EXPECT_TRUE(behavior.snapshot().click_through);
    behavior.restore_interaction();
    EXPECT_TRUE(!behavior.snapshot().click_through);
    EXPECT_EQ(fixture.click_through_changes.size(), std::size_t{2});
    EXPECT_TRUE(!fixture.click_through_changes.back());
}

TEST_CASE(window_behavior_optional_thirty_minute_recovery_restores_interaction) {
    BehaviorFixture fixture;
    WindowBehavior behavior{fixture.api()};
    behavior.set_auto_restore(std::chrono::minutes{30});

    EXPECT_TRUE(behavior.set_click_through(true));
    EXPECT_TRUE(behavior.snapshot().restore_at.has_value());
    fixture.now += std::chrono::minutes{29};
    EXPECT_TRUE(!behavior.poll_timeout());
    fixture.now += std::chrono::minutes{1};
    EXPECT_TRUE(behavior.poll_timeout());
    EXPECT_TRUE(!behavior.snapshot().click_through);
    EXPECT_TRUE(!behavior.snapshot().restore_at.has_value());
}

TEST_CASE(window_behavior_startup_refuses_unsafe_saved_click_through_state) {
    BehaviorFixture fixture;
    fixture.hotkey_available = false;
    WindowBehavior behavior{fixture.api()};

    EXPECT_TRUE(!behavior.initialize(WindowLayer::top, true));
    EXPECT_EQ(behavior.snapshot().layer, WindowLayer::top);
    EXPECT_TRUE(!behavior.snapshot().click_through);
    EXPECT_EQ(fixture.click_through_changes.size(), std::size_t{0});
}
