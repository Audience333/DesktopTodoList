#include "test_support.h"

#include "domain/selection_model.h"
#include "domain/task_store.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::SelectionModel;
using desktop_todo::Task;
using desktop_todo::TaskStore;

const std::vector<std::wstring> visible{L"a", L"b", L"c", L"d", L"e"};

bool contains(const std::vector<std::wstring>& ids, std::wstring_view id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

Task task(std::wstring id) {
    Task result;
    result.id = std::move(id);
    result.title = result.id;
    result.created_at = Clock::time_point{std::chrono::milliseconds{1}};
    result.updated_at = result.created_at;
    return result;
}

}  // namespace

TEST_CASE(selection_model_select_one_moves_and_toggles_single_selection) {
    SelectionModel selection;

    selection.select_one(L"b");
    auto snapshot = selection.snapshot();
    EXPECT_EQ(snapshot.selected_ids, std::vector<std::wstring>{L"b"});
    EXPECT_EQ(snapshot.anchor_id, std::optional<std::wstring>{L"b"});
    EXPECT_EQ(snapshot.focus_id, std::optional<std::wstring>{L"b"});

    selection.select_one(L"c");
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"c"});
    selection.select_one(L"c");
    EXPECT_TRUE(selection.snapshot().selected_ids.empty());
}

TEST_CASE(selection_model_toggle_supports_ctrl_style_selection) {
    SelectionModel selection;

    selection.toggle(L"a");
    selection.toggle(L"c");
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>({L"a", L"c"}));

    selection.toggle(L"a");
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"c"});
    EXPECT_EQ(selection.snapshot().anchor_id, std::optional<std::wstring>{L"a"});
}

TEST_CASE(selection_model_select_range_handles_forward_reverse_and_append) {
    SelectionModel selection;
    selection.select_one(L"b");

    selection.select_range(L"e", visible, false);
    EXPECT_EQ(
        selection.snapshot().selected_ids,
        std::vector<std::wstring>({L"b", L"c", L"d", L"e"}));

    selection.select_one(L"e");
    selection.select_range(L"b", visible, false);
    EXPECT_EQ(
        selection.snapshot().selected_ids,
        std::vector<std::wstring>({L"b", L"c", L"d", L"e"}));

    selection.toggle(L"a");
    selection.select_range(L"c", visible, true);
    const auto appended = selection.snapshot().selected_ids;
    EXPECT_TRUE(contains(appended, L"a"));
    EXPECT_TRUE(contains(appended, L"b"));
    EXPECT_TRUE(contains(appended, L"c"));
}

TEST_CASE(selection_model_toggle_all_visible_preserves_hidden_selection) {
    SelectionModel selection;
    selection.toggle(L"hidden");

    selection.toggle_all_visible({L"a", L"b"});
    auto selected = selection.snapshot().selected_ids;
    EXPECT_TRUE(contains(selected, L"hidden"));
    EXPECT_TRUE(contains(selected, L"a"));
    EXPECT_TRUE(contains(selected, L"b"));

    selection.toggle_all_visible({L"a", L"b"});
    selected = selection.snapshot().selected_ids;
    EXPECT_EQ(selected, std::vector<std::wstring>{L"hidden"});
}

TEST_CASE(selection_model_move_focus_and_shift_extension_are_bounded) {
    SelectionModel selection;
    selection.select_one(L"b");

    EXPECT_EQ(selection.move_focus(visible, 1, false), std::optional<std::wstring>{L"c"});
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"c"});
    EXPECT_EQ(selection.move_focus(visible, 2, true), std::optional<std::wstring>{L"e"});
    EXPECT_EQ(
        selection.snapshot().selected_ids,
        std::vector<std::wstring>({L"c", L"d", L"e"}));
    EXPECT_EQ(selection.move_focus(visible, 1, false), std::optional<std::wstring>{L"e"});
}

TEST_CASE(selection_model_remove_missing_cleans_ids_anchor_and_focus) {
    SelectionModel selection;
    selection.toggle(L"a");
    selection.toggle(L"missing");

    selection.remove_missing({L"a", L"b"});
    const auto snapshot = selection.snapshot();

    EXPECT_EQ(snapshot.selected_ids, std::vector<std::wstring>{L"a"});
    EXPECT_TRUE(!snapshot.anchor_id.has_value());
    EXPECT_TRUE(!snapshot.focus_id.has_value());
}

TEST_CASE(selection_model_completion_checkbox_is_independent_of_selection) {
    AppState state;
    state.tasks = {task(L"a"), task(L"b")};
    auto now = Clock::time_point{std::chrono::milliseconds{1'000}};
    TaskStore store{
        std::move(state),
        [&now] { return now; },
        [] { return L"new"; }};
    SelectionModel selection;
    selection.select_one(L"b");
    const auto before = selection.snapshot();

    EXPECT_TRUE(store.set_completed(L"a", true));

    EXPECT_EQ(selection.snapshot().selected_ids, before.selected_ids);
    EXPECT_EQ(selection.snapshot().anchor_id, before.anchor_id);
    EXPECT_EQ(selection.snapshot().focus_id, before.focus_id);
}

TEST_CASE(selection_model_keyboard_focus_starts_at_edge_and_does_not_toggle_at_boundary) {
    SelectionModel selection;

    EXPECT_EQ(selection.move_focus(visible, 1, false), std::optional<std::wstring>{L"a"});
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"a"});
    EXPECT_EQ(selection.move_focus(visible, -1, false), std::optional<std::wstring>{L"a"});
    EXPECT_EQ(selection.snapshot().selected_ids, std::vector<std::wstring>{L"a"});
}
