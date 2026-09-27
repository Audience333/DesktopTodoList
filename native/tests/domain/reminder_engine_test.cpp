#include "test_support.h"

#include "domain/reminder_engine.h"
#include "domain/validation.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::Task;
using desktop_todo::TaskStatus;

Clock::time_point at(std::int64_t milliseconds) {
    return Clock::time_point{std::chrono::milliseconds{milliseconds}};
}

Task reminder_task(std::wstring id, Clock::time_point due_at) {
    Task task;
    task.id = std::move(id);
    task.title = L"提醒 " + task.id;
    task.note = L"显示详情";
    task.due_at = due_at;
    task.created_at = at(1);
    task.updated_at = at(1);
    return task;
}

std::vector<std::wstring> ids(const desktop_todo::ReminderBatch& batch) {
    std::vector<std::wstring> result;
    result.reserve(batch.items.size());
    for (const auto& item : batch.items) {
        result.push_back(item.task_id);
    }
    return result;
}

}  // namespace

TEST_CASE(reminder_engine_honors_zero_five_ten_and_thirty_minute_leads) {
    const auto now = at(1'800'000'000'000);
    for (const int minutes : {0, 5, 10, 30}) {
        AppState state;
        state.settings.remind_advance_minutes = minutes;
        state.tasks = {reminder_task(
            L"m" + std::to_wstring(minutes), now + std::chrono::minutes{minutes})};

        const auto batch = desktop_todo::evaluate_tick_reminders(state, now);

        EXPECT_EQ(batch.items.size(), std::size_t{1});
    }
}

TEST_CASE(reminder_engine_startup_aggregates_multiple_due_tasks) {
    const auto now = at(1'800'000'000'000);
    AppState state;
    state.tasks = {
        reminder_task(L"a", now - std::chrono::minutes{10}),
        reminder_task(L"b", now - std::chrono::minutes{5})};

    const auto batch = desktop_todo::evaluate_startup_reminders(state, now);

    EXPECT_TRUE(batch.startup);
    EXPECT_TRUE(batch.grouped);
    EXPECT_EQ(ids(batch), std::vector<std::wstring>({L"a", L"b"}));
    EXPECT_EQ(batch.items[0].title, L"提醒 a");
    EXPECT_EQ(batch.items[0].note, L"显示详情");
}

TEST_CASE(reminder_engine_tick_delivers_only_when_threshold_is_crossed) {
    const auto due = at(1'800'000'600'000);
    AppState state;
    state.tasks = {reminder_task(L"a", due)};

    const auto early = desktop_todo::evaluate_tick_reminders(
        state, due - std::chrono::milliseconds{1});
    const auto due_now = desktop_todo::evaluate_tick_reminders(state, due);

    EXPECT_TRUE(early.items.empty());
    EXPECT_EQ(ids(due_now), std::vector<std::wstring>{L"a"});
    EXPECT_TRUE(!due_now.startup);
    EXPECT_TRUE(!due_now.grouped);
}

TEST_CASE(reminder_engine_reminded_at_deduplicates_delivery) {
    const auto now = at(1'800'000'000'000);
    AppState state;
    auto task = reminder_task(L"a", now - std::chrono::minutes{1});
    task.reminded_at = now - std::chrono::seconds{30};
    state.tasks = {task};

    EXPECT_TRUE(desktop_todo::evaluate_tick_reminders(state, now).items.empty());
    EXPECT_TRUE(desktop_todo::evaluate_startup_reminders(state, now).items.empty());
}

TEST_CASE(reminder_engine_schedule_change_resets_deduplication) {
    const auto now = at(1'800'000'000'000);
    auto before = reminder_task(L"a", now + std::chrono::hours{1});
    before.reminded_at = now;

    auto changed_due = before;
    changed_due.due_at = now + std::chrono::hours{2};
    changed_due = desktop_todo::reset_reminder_if_schedule_changed(before, changed_due);
    EXPECT_TRUE(!changed_due.reminded_at.has_value());

    auto changed_switch = before;
    changed_switch.remind = false;
    changed_switch = desktop_todo::reset_reminder_if_schedule_changed(before, changed_switch);
    EXPECT_TRUE(!changed_switch.reminded_at.has_value());
}

TEST_CASE(reminder_engine_completion_and_restore_keep_schedule_identity) {
    const auto now = at(1'800'000'000'000);
    auto before = reminder_task(L"a", now + std::chrono::hours{1});
    before.reminded_at = now;

    auto completed = before;
    completed.status = TaskStatus::done;
    completed = desktop_todo::reset_reminder_if_schedule_changed(before, completed);
    EXPECT_EQ(completed.reminded_at, before.reminded_at);

    auto restored = completed;
    restored.status = TaskStatus::todo;
    restored = desktop_todo::reset_reminder_if_schedule_changed(completed, restored);
    EXPECT_EQ(restored.reminded_at, before.reminded_at);
}

TEST_CASE(reminder_engine_tick_after_sleep_catches_crossed_deadline) {
    const auto due = at(1'800'000'600'000);
    AppState state;
    state.tasks = {reminder_task(L"sleep", due)};

    const auto batch = desktop_todo::evaluate_tick_reminders(
        state, due + std::chrono::hours{3});

    EXPECT_EQ(ids(batch), std::vector<std::wstring>{L"sleep"});
    EXPECT_TRUE(batch.items[0].overdue);
}

TEST_CASE(reminder_engine_ignores_disabled_completed_and_missing_due_tasks) {
    const auto now = at(1'800'000'000'000);
    AppState state;
    auto disabled = reminder_task(L"disabled", now);
    disabled.remind = false;
    auto completed = reminder_task(L"completed", now);
    completed.status = TaskStatus::done;
    completed.completed_at = now;
    auto missing_due = reminder_task(L"missing", now);
    missing_due.due_at.reset();
    const auto validated = desktop_todo::validate_task(missing_due, 1.0, now);
    EXPECT_TRUE(validated.task.has_value());
    state.tasks = {disabled, completed, *validated.task};

    EXPECT_TRUE(desktop_todo::evaluate_tick_reminders(state, now).items.empty());
}

TEST_CASE(reminder_engine_dst_fallback_instants_remain_distinct) {
    // 2026-11-01 01:30 occurs twice in New York: 05:30Z and 06:30Z.
    const auto first_utc = at(1'793'511'000'000);
    const auto second_utc = first_utc + std::chrono::hours{1};
    AppState state;
    state.tasks = {
        reminder_task(L"first-0130", first_utc),
        reminder_task(L"second-0130", second_utc)};

    const auto first_batch = desktop_todo::evaluate_tick_reminders(state, first_utc);
    EXPECT_EQ(ids(first_batch), std::vector<std::wstring>{L"first-0130"});

    state.tasks[0].reminded_at = first_utc;
    const auto second_batch = desktop_todo::evaluate_tick_reminders(state, second_utc);
    EXPECT_EQ(ids(second_batch), std::vector<std::wstring>{L"second-0130"});
}
