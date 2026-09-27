#include "domain/reminder_engine.h"

#include <algorithm>
#include <chrono>

namespace desktop_todo {
namespace {

ReminderBatch evaluate(
    const AppState& state,
    Clock::time_point now,
    bool startup) {
    ReminderBatch batch;
    batch.startup = startup;
    const auto lead_minutes = std::max(0, state.settings.remind_advance_minutes);
    const auto lead = std::chrono::minutes{lead_minutes};

    for (const auto& task : state.tasks) {
        if (task.status == TaskStatus::done || !task.remind ||
            !task.due_at.has_value() || task.reminded_at.has_value()) {
            continue;
        }
        if (*task.due_at - lead > now) {
            continue;
        }
        batch.items.push_back({
            task.id,
            task.title,
            task.note,
            *task.due_at,
            *task.due_at < now});
    }
    batch.grouped = startup && batch.items.size() > 1;
    return batch;
}

}  // namespace

ReminderBatch evaluate_startup_reminders(
    const AppState& state,
    Clock::time_point now) {
    return evaluate(state, now, true);
}

ReminderBatch evaluate_tick_reminders(
    const AppState& state,
    Clock::time_point now) {
    return evaluate(state, now, false);
}

Task reset_reminder_if_schedule_changed(const Task& before, Task after) {
    if (before.due_at != after.due_at || before.remind != after.remind) {
        after.reminded_at.reset();
    }
    return after;
}

}  // namespace desktop_todo
