#include "test_support.h"

#include "domain/selection_model.h"
#include "presentation/pointer_controller.h"
#include "presentation/selection_toolbar.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>

using desktop_todo::PointF;
using desktop_todo::PointerActionKind;
using desktop_todo::PointerController;
using desktop_todo::PointerRowBounds;
using desktop_todo::RowHitArea;
using desktop_todo::SelectionModel;

TEST_CASE(pointer_controller_checkbox_does_not_change_selection_and_title_starts_edit) {
    SelectionModel selection;
    selection.select_one(L"a");
    PointerController pointer{selection};
    const std::vector<std::wstring> visible{L"a", L"b", L"c", L"d"};

    const auto checkbox = pointer.press(RowHitArea::checkbox, L"b", {10, 10}, visible, false, false);
    EXPECT_EQ(checkbox.kind, PointerActionKind::toggle_completion);
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"a"});

    const auto title = pointer.press(RowHitArea::title, L"b", {10, 10}, visible, false, false);
    EXPECT_EQ(title.kind, PointerActionKind::begin_inline_edit);
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"a"});
}

TEST_CASE(pointer_controller_row_click_supports_ctrl_and_shift_selection) {
    SelectionModel selection;
    PointerController pointer{selection};
    const std::vector<std::wstring> visible{L"a", L"b", L"c", L"d"};

    static_cast<void>(pointer.press(RowHitArea::row, L"a", {10, 10}, visible, false, false));
    static_cast<void>(pointer.press(RowHitArea::row, L"c", {10, 10}, visible, true, false));
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>({L"a", L"c"}));
    static_cast<void>(pointer.press(RowHitArea::row, L"d", {10, 10}, visible, false, true));
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>({L"c", L"d"}));
}

TEST_CASE(pointer_controller_respects_disabled_multi_select_and_rubber_band_settings) {
    SelectionModel selection;
    selection.select_one(L"a");
    PointerController pointer{selection};
    pointer.set_features(false, false);
    const std::vector<std::wstring> visible{L"a", L"b"};

    static_cast<void>(pointer.press(RowHitArea::row, L"b", {10, 10}, visible, true, false));
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"b"});

    static_cast<void>(pointer.press(RowHitArea::none, {}, {0, 0}, {}, false, false));
    const auto action = pointer.move({20, 20}, {{L"a", {0, 0, 30, 10}}});
    EXPECT_EQ(action.kind, PointerActionKind::none);
    EXPECT_TRUE(!pointer.active());
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"b"});
}

TEST_CASE(pointer_controller_blank_drag_rubber_bands_and_ctrl_appends) {
    SelectionModel selection;
    selection.select_one(L"hidden");
    PointerController pointer{selection};
    const std::vector<PointerRowBounds> rows{
        {L"a", {0, 20, 100, 20}}, {L"b", {0, 45, 100, 20}},
        {L"c", {0, 70, 100, 20}}, {L"d", {0, 95, 100, 20}}};

    static_cast<void>(pointer.press(RowHitArea::none, {}, {0, 0}, {}, false, false));
    const auto band = pointer.move({80, 65}, rows);
    EXPECT_EQ(band.kind, PointerActionKind::rubber_band);
    EXPECT_TRUE(pointer.capturing());
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>({L"a", L"b"}));
    pointer.cancel();
    EXPECT_TRUE(!pointer.capturing());
    EXPECT_TRUE(!pointer.active());

    static_cast<void>(pointer.press(RowHitArea::none, {}, {0, 0}, {}, true, false));
    static_cast<void>(pointer.move({80, 90}, rows));
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>({L"a", L"b", L"c"}));
    pointer.cancel();
}

TEST_CASE(pointer_controller_drag_handle_drops_before_or_after_target) {
    SelectionModel selection;
    PointerController pointer{selection};
    const std::vector<std::wstring> visible{L"a", L"b", L"c"};
    const std::vector<PointerRowBounds> rows{
        {L"a", {0, 0, 100, 20}}, {L"b", {0, 25, 100, 20}}, {L"c", {0, 50, 100, 20}}};

    const auto begin = pointer.press(RowHitArea::drag_handle, L"a", {10, 10}, visible, false, false);
    EXPECT_EQ(begin.kind, PointerActionKind::begin_drag);
    const auto drop = pointer.release({50, 42}, rows);
    EXPECT_EQ(drop.kind, PointerActionKind::reorder);
    EXPECT_EQ(drop.task_id, L"a");
    EXPECT_EQ(drop.target_id, L"b");
    EXPECT_EQ(drop.position, desktop_todo::DropPosition::after);
    EXPECT_TRUE(!pointer.capturing());

    static_cast<void>(pointer.press(RowHitArea::drag_handle, L"a", {10, 10}, visible, false, false));
    EXPECT_TRUE(pointer.capturing());
    pointer.cancel();
    EXPECT_TRUE(!pointer.capturing());
    EXPECT_TRUE(!pointer.active());
}

TEST_CASE(selection_toolbar_counts_hidden_selection) {
    const auto state = desktop_todo::selection_toolbar_state(
        {L"a", L"hidden-1", L"hidden-2"}, {L"a", L"b"});
    EXPECT_TRUE(state.visible);
    EXPECT_EQ(state.selected_count, std::size_t{3});
    EXPECT_EQ(state.hidden_count, std::size_t{2});
}

TEST_CASE(selection_toolbar_hit_testing_maps_each_action_without_gaps) {
    constexpr desktop_todo::RectF bounds{16, 436, 328, 28};
    constexpr desktop_todo::SelectionToolbarAction expected[]{
        desktop_todo::SelectionToolbarAction::complete,
        desktop_todo::SelectionToolbarAction::high_priority,
        desktop_todo::SelectionToolbarAction::edit_details,
        desktop_todo::SelectionToolbarAction::delete_selected,
        desktop_todo::SelectionToolbarAction::clear};
    const auto label_width = std::min(86.0F, std::max(58.0F, bounds.width * 0.26F));
    const auto gap = 3.0F;
    const auto button_width = (bounds.width - label_width - gap * 5.0F) / 5.0F;
    for (std::size_t index = 0; index < std::size(expected); ++index) {
        const auto x = bounds.x + label_width + gap +
            static_cast<float>(index) * (button_width + gap) + button_width / 2;
        EXPECT_EQ(desktop_todo::selection_toolbar_action_at(x, bounds.y + 10, bounds),
            std::optional{expected[index]});
    }
    EXPECT_TRUE(!desktop_todo::selection_toolbar_action_at(bounds.x + 1, bounds.y + 10, bounds));
}
