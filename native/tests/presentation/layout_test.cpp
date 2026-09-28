#include "test_support.h"

#include "presentation/layout.h"
#include "presentation/theme.h"

using desktop_todo::LayoutMode;
using desktop_todo::MonitorInfo;
using desktop_todo::RectI;
using desktop_todo::SizeF;
using desktop_todo::SystemTheme;
using desktop_todo::Theme;

TEST_CASE(layout_default_and_minimum_sizes_scale_for_dpi) {
    EXPECT_EQ(desktop_todo::default_widget_size(96.0F), SizeF({360.0F, 480.0F}));
    EXPECT_EQ(desktop_todo::default_widget_size(120.0F), SizeF({450.0F, 600.0F}));
    EXPECT_EQ(desktop_todo::default_widget_size(144.0F), SizeF({540.0F, 720.0F}));
    EXPECT_EQ(desktop_todo::default_widget_size(192.0F), SizeF({720.0F, 960.0F}));
    EXPECT_EQ(desktop_todo::minimum_widget_width(144.0F), 480.0F);
}

TEST_CASE(layout_regions_are_ordered_in_compact_and_expanded_modes) {
    const auto compact = desktop_todo::calculate_layout(
        {450.0F, 600.0F}, 120.0F, LayoutMode::compact);
    const auto expanded = desktop_todo::calculate_layout(
        {450.0F, 600.0F}, 120.0F, LayoutMode::expanded);

    EXPECT_EQ(compact.logical_client, SizeF({360.0F, 480.0F}));
    EXPECT_TRUE(compact.title.bottom() <= compact.quick_add.y);
    EXPECT_TRUE(compact.quick_add.bottom() <= compact.tabs.y);
    EXPECT_TRUE(compact.tabs.bottom() <= compact.task_list.y);
    EXPECT_TRUE(expanded.details.has_value());
    EXPECT_TRUE(expanded.task_list.width < compact.task_list.width);
}

TEST_CASE(layout_clamps_saved_window_to_remaining_monitor_work_area) {
    const MonitorInfo monitor{{0, 0, 1920, 1040}, 144.0F};
    EXPECT_EQ(desktop_todo::clamp_to_work_area({2400, -600, 540, 720}, monitor),
        RectI({1380, 0, 540, 720}));
    EXPECT_EQ(desktop_todo::clamp_to_work_area({100, 100, 2500, 1400}, monitor),
        RectI({0, 0, 1920, 1040}));
}

TEST_CASE(layout_theme_resolves_system_dark_high_contrast_and_motion) {
    const auto light = desktop_todo::resolve_theme(
        Theme::system, SystemTheme{.dark = false, .high_contrast = false, .reduced_motion = false});
    const auto dark = desktop_todo::resolve_theme(
        Theme::dark, SystemTheme{.dark = false, .high_contrast = false, .reduced_motion = true});
    const auto contrast = desktop_todo::resolve_theme(
        Theme::light, SystemTheme{.dark = false, .high_contrast = true, .reduced_motion = false});

    EXPECT_TRUE(light.background != dark.background);
    EXPECT_TRUE(dark.reduced_motion);
    EXPECT_TRUE(contrast.high_contrast);
    EXPECT_TRUE(contrast.background != contrast.foreground);
}
