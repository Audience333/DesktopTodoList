#include "test_support.h"

#include "presentation/task_list_view.h"
#include "presentation/view_model.h"

#include <chrono>
#include <string>
#include <vector>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::Priority;
using desktop_todo::QuerySpec;
using desktop_todo::Task;
using desktop_todo::TaskStatus;
using desktop_todo::ViewKind;

Clock::time_point instant(int year, unsigned month, unsigned day, int hour = 0) {
    using namespace std::chrono;
    const auto date = sys_days{std::chrono::year{year} / month / day};
    return Clock::time_point{duration_cast<milliseconds>(date.time_since_epoch())} + hours{hour};
}

Task task(std::wstring id, std::wstring title, double order,
    std::optional<Clock::time_point> due = std::nullopt,
    TaskStatus status = TaskStatus::todo) {
    Task value;
    value.id = std::move(id);
    value.title = std::move(title);
    value.order = order;
    value.due_at = due;
    value.status = status;
    value.priority = Priority::medium;
    if (status == TaskStatus::done) value.completed_at = instant(2026, 9, 20);
    return value;
}

}  // namespace

TEST_CASE(task_list_view_builds_today_week_all_and_done_rows_and_counts) {
    AppState state;
    const auto now = instant(2026, 9, 23, 12);
    state.tasks = {
        task(L"overdue", L"Overdue", 1, instant(2026, 9, 22)),
        task(L"today", L"Today", 2, instant(2026, 9, 23, 18)),
        task(L"week", L"This week", 3, instant(2026, 9, 25)),
        task(L"later", L"Later", 4, instant(2026, 10, 5)),
        task(L"undated", L"No date", 5),
        task(L"done", L"Completed", 6, std::nullopt, TaskStatus::done)};

    const auto today = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::today}, now);
    const auto week = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::week}, now);
    const auto all = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::all}, now);
    const auto done = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::done}, now);

    EXPECT_EQ(today.rows.size(), std::size_t{2});
    EXPECT_EQ(week.rows.size(), std::size_t{3});
    EXPECT_EQ(all.rows.size(), std::size_t{5});
    EXPECT_EQ(done.rows.size(), std::size_t{1});
    EXPECT_EQ(all.counts.overdue, std::size_t{1});
    EXPECT_EQ(done.rows.front().id, L"done");
}

TEST_CASE(task_list_view_empty_snapshot_has_zero_counts_and_rows) {
    const AppState state;
    const auto model = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::today}, instant(2026, 9, 23));

    EXPECT_TRUE(model.rows.empty());
    EXPECT_EQ(model.counts.today, std::size_t{0});
    EXPECT_EQ(model.counts.week, std::size_t{0});
    EXPECT_EQ(model.counts.all, std::size_t{0});
    EXPECT_EQ(model.counts.done, std::size_t{0});
    EXPECT_EQ(model.counts.overdue, std::size_t{0});
}

TEST_CASE(task_list_view_marks_overdue_and_emits_search_highlight_spans) {
    AppState state;
    auto overdue = task(L"a", L"Write REPORT today", 1, instant(2026, 9, 22));
    overdue.note = L"report notes";
    state.tasks = {overdue};

    const auto model = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::all, .search = L"report"}, instant(2026, 9, 23));

    EXPECT_EQ(model.rows.size(), std::size_t{1});
    EXPECT_TRUE(model.rows.front().overdue);
    EXPECT_EQ(model.rows.front().highlights.size(), std::size_t{2});
    EXPECT_EQ(model.rows.front().highlights[0].start, std::size_t{6});
    EXPECT_EQ(model.rows.front().highlights[0].length, std::size_t{6});
}

TEST_CASE(task_list_view_builds_large_snapshots_for_a_virtualized_viewport) {
    AppState state;
    const auto now = instant(2026, 9, 23, 12);
    state.tasks.reserve(1'000);
    for (int index = 0; index < 1'000; ++index) {
        state.tasks.push_back(task(L"task-" + std::to_wstring(index),
            L"Task " + std::to_wstring(index), static_cast<double>(index),
            now + std::chrono::hours{index % 96}));
    }

    const auto model = desktop_todo::build_view_model(
        state, QuerySpec{.view = ViewKind::all}, now);
    const auto visible = desktop_todo::calculate_visible_range(
        8'000, 240, 58, model.rows.size(), 2);

    EXPECT_EQ(model.rows.size(), std::size_t{1'000});
    EXPECT_TRUE(visible.last - visible.first < model.rows.size());
    EXPECT_EQ(visible.first, std::size_t{135});
}

TEST_CASE(task_list_view_virtual_range_handles_empty_single_and_large_lists) {
    EXPECT_EQ(desktop_todo::calculate_visible_range(0, 200, 40, 0, 2),
        desktop_todo::VisibleRange({0, 0}));
    EXPECT_EQ(desktop_todo::calculate_visible_range(0, 200, 40, 1, 2),
        desktop_todo::VisibleRange({0, 1}));
    EXPECT_EQ(desktop_todo::calculate_visible_range(400, 200, 40, 1'000, 2),
        desktop_todo::VisibleRange({8, 17}));
    EXPECT_EQ(desktop_todo::calculate_visible_range(39, 80, 40, 200, 2),
        desktop_todo::VisibleRange({0, 5}));
}

TEST_CASE(task_list_view_preserves_scroll_anchor_when_rows_are_inserted_or_removed) {
    const std::vector<std::wstring> before{L"a", L"b", L"c", L"d"};
    EXPECT_EQ(desktop_todo::preserve_scroll_anchor(
        before, {L"x", L"a", L"b", L"c", L"d"}, 2), std::size_t{3});
    EXPECT_EQ(desktop_todo::preserve_scroll_anchor(
        before, {L"a", L"c", L"d"}, 1), std::size_t{1});
    EXPECT_EQ(desktop_todo::preserve_scroll_anchor(
        before, {}, 3), std::size_t{0});
}

TEST_CASE(task_list_view_hit_targets_are_distinct_and_outside_is_empty) {
    using desktop_todo::PointF;
    using desktop_todo::RectF;
    using desktop_todo::RowHitArea;
    const desktop_todo::RowHitZones zones{
        .row = {0, 0, 360, 48},
        .checkbox = {8, 8, 32, 32},
        .title = {48, 4, 240, 40},
        .delete_button = {304, 8, 24, 32},
        .drag_handle = {336, 8, 16, 32}};

    EXPECT_EQ(desktop_todo::hit_test_row({24, 24}, zones), RowHitArea::checkbox);
    EXPECT_EQ(desktop_todo::hit_test_row({120, 24}, zones), RowHitArea::title);
    EXPECT_EQ(desktop_todo::hit_test_row({300, 24}, zones), RowHitArea::row);
    EXPECT_EQ(desktop_todo::hit_test_row({312, 24}, zones), RowHitArea::delete_button);
    EXPECT_EQ(desktop_todo::hit_test_row({344, 24}, zones), RowHitArea::drag_handle);
    EXPECT_EQ(desktop_todo::hit_test_row({380, 24}, zones), RowHitArea::none);
}
