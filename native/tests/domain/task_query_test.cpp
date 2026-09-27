#include "test_support.h"

#include "domain/task_query.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::Priority;
using desktop_todo::QuerySpec;
using desktop_todo::Task;
using desktop_todo::TaskRef;
using desktop_todo::TaskStatus;
using desktop_todo::ViewKind;

Clock::time_point instant(
    int year,
    unsigned month,
    unsigned day,
    int hour = 0,
    int minute = 0) {
    using namespace std::chrono;
    const auto date = sys_days{std::chrono::year{year} / month / day};
    return Clock::time_point{duration_cast<milliseconds>(date.time_since_epoch())} +
        hours{hour} + minutes{minute};
}

Task make_task(
    std::wstring id,
    double order,
    std::optional<Clock::time_point> due_at = std::nullopt,
    Priority priority = Priority::medium,
    TaskStatus status = TaskStatus::todo) {
    Task task;
    task.id = std::move(id);
    task.title = task.id;
    task.order = order;
    task.due_at = due_at;
    task.priority = priority;
    task.status = status;
    task.created_at = instant(2026, 1, 1);
    task.updated_at = task.created_at;
    if (status == TaskStatus::done) {
        task.completed_at = instant(2026, 9, 20);
    }
    return task;
}

std::vector<std::wstring> ids(const std::vector<TaskRef>& tasks) {
    std::vector<std::wstring> result;
    result.reserve(tasks.size());
    for (const auto& task : tasks) {
        result.push_back(task.task->id);
    }
    return result;
}

}  // namespace

TEST_CASE(task_query_today_includes_overdue_and_same_day_only) {
    AppState state;
    state.tasks = {
        make_task(L"overdue", 1.0, instant(2026, 9, 22, 18)),
        make_task(L"today", 2.0, instant(2026, 9, 23, 23, 59)),
        make_task(L"tomorrow", 3.0, instant(2026, 9, 24, 8)),
        make_task(L"undated", 4.0),
        make_task(L"done", 5.0, instant(2026, 9, 23), Priority::medium, TaskStatus::done)};

    const auto result = desktop_todo::query_tasks(
        state, QuerySpec{.view = ViewKind::today}, instant(2026, 9, 23, 12));

    EXPECT_EQ(ids(result), std::vector<std::wstring>({L"overdue", L"today"}));
}

TEST_CASE(task_query_week_honors_monday_and_sunday_boundaries) {
    AppState state;
    state.tasks = {
        make_task(L"sunday", 1.0, instant(2026, 9, 27, 12)),
        make_task(L"monday", 2.0, instant(2026, 9, 28, 12))};
    const auto now = instant(2026, 9, 27, 9);

    const auto monday_start = desktop_todo::query_tasks(
        state, QuerySpec{.view = ViewKind::week, .week_starts_on = 1}, now);
    const auto sunday_start = desktop_todo::query_tasks(
        state, QuerySpec{.view = ViewKind::week, .week_starts_on = 0}, now);

    EXPECT_EQ(ids(monday_start), std::vector<std::wstring>{L"sunday"});
    EXPECT_EQ(ids(sunday_start), std::vector<std::wstring>({L"sunday", L"monday"}));
}

TEST_CASE(task_query_all_and_done_views_keep_statuses_separate) {
    AppState state;
    state.tasks = {
        make_task(L"todo", 1.0),
        make_task(L"done", 2.0, std::nullopt, Priority::medium, TaskStatus::done)};

    const auto pending = desktop_todo::query_tasks(
        state, QuerySpec{.view = ViewKind::all}, instant(2026, 9, 23));
    const auto completed = desktop_todo::query_tasks(
        state, QuerySpec{.view = ViewKind::done}, instant(2026, 9, 23));

    EXPECT_EQ(ids(pending), std::vector<std::wstring>{L"todo"});
    EXPECT_EQ(ids(completed), std::vector<std::wstring>{L"done"});
}

TEST_CASE(task_query_searches_title_and_note_case_insensitively) {
    AppState state;
    auto title_match = make_task(L"title", 1.0);
    title_match.title = L"Write REPORT";
    auto note_match = make_task(L"note", 2.0);
    note_match.note = L"report data";
    auto miss = make_task(L"miss", 3.0);
    miss.title = L"unrelated";
    state.tasks = {title_match, note_match, miss};

    const auto result = desktop_todo::query_tasks(
        state,
        QuerySpec{.view = ViewKind::all, .search = L"RePoRt"},
        instant(2026, 9, 23));

    EXPECT_EQ(ids(result), std::vector<std::wstring>({L"title", L"note"}));
}

TEST_CASE(task_query_sorts_status_overdue_priority_due_and_manual_order) {
    AppState state;
    const auto now = instant(2026, 9, 23, 12);
    state.tasks = {
        make_task(L"medium", 1.0, instant(2026, 9, 24), Priority::medium),
        make_task(L"high-far", 8.0, instant(2026, 9, 26), Priority::high),
        make_task(L"overdue-low", 9.0, instant(2026, 9, 22), Priority::low),
        make_task(L"high-near-later-order", 3.0, instant(2026, 9, 24), Priority::high),
        make_task(L"high-near-earlier-order", 2.0, instant(2026, 9, 24), Priority::high),
        make_task(L"done", 0.0, std::nullopt, Priority::high, TaskStatus::done)};

    const auto result = desktop_todo::query_tasks(
        state,
        QuerySpec{.view = ViewKind::all, .include_completed = true},
        now);

    EXPECT_EQ(
        ids(result),
        std::vector<std::wstring>({
            L"overdue-low",
            L"high-near-earlier-order",
            L"high-near-later-order",
            L"high-far",
            L"medium",
            L"done"}));
}

TEST_CASE(task_query_1000_tasks_is_stable) {
    AppState state;
    const auto now = instant(2026, 9, 23, 12);
    state.tasks.reserve(1'000);
    for (int index = 0; index < 1'000; ++index) {
        const auto priority = index % 3 == 0 ? Priority::high
            : index % 3 == 1 ? Priority::medium
                             : Priority::low;
        state.tasks.push_back(make_task(
            L"task-" + std::to_wstring(index),
            static_cast<double>(1'000 - index),
            now + std::chrono::hours{index % 48 - 12},
            priority));
    }

    const QuerySpec query{.view = ViewKind::all};
    const auto first = ids(desktop_todo::query_tasks(state, query, now));
    const auto second = ids(desktop_todo::query_tasks(state, query, now));

    EXPECT_EQ(first.size(), std::size_t{1'000});
    EXPECT_EQ(first, second);
}

TEST_CASE(task_query_dense_orders_compact_to_consecutive_values) {
    std::vector<Task> tasks = {
        make_task(L"a", 20.0),
        make_task(L"b", 20.0 + 1e-8),
        make_task(L"c", 2'000'000.0)};

    desktop_todo::compact_order(tasks);

    EXPECT_EQ(tasks[0].id, L"a");
    EXPECT_EQ(tasks[1].id, L"b");
    EXPECT_EQ(tasks[2].id, L"c");
    EXPECT_EQ(tasks[0].order, 1.0);
    EXPECT_EQ(tasks[1].order, 2.0);
    EXPECT_EQ(tasks[2].order, 3.0);
}

TEST_CASE(task_query_today_uses_supplied_local_utc_offset) {
    AppState state;
    state.tasks = {make_task(L"local-today", 1.0, instant(2026, 9, 26, 16, 30))};
    const auto now = instant(2026, 9, 26, 16, 10);

    const auto result = desktop_todo::query_tasks(
        state,
        QuerySpec{.view = ViewKind::today, .utc_offset = std::chrono::hours{8}},
        now);

    EXPECT_EQ(ids(result), std::vector<std::wstring>{L"local-today"});
}

TEST_CASE(task_query_compacts_shuffled_dense_orders_by_manual_order) {
    std::vector<Task> tasks = {
        make_task(L"third", 3.0),
        make_task(L"first", 1.0),
        make_task(L"second", 1.0 + 1e-8)};

    desktop_todo::compact_order(tasks);

    EXPECT_EQ(tasks[0].order, 3.0);
    EXPECT_EQ(tasks[1].order, 1.0);
    EXPECT_EQ(tasks[2].order, 2.0);
}
