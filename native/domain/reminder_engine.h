#pragma once

#include "domain/types.h"

#include <string>
#include <vector>

namespace desktop_todo {

struct ReminderItem {
    std::wstring task_id;
    std::wstring title;
    std::wstring note;
    Clock::time_point due_at{};
    bool overdue = false;
};

struct ReminderBatch {
    std::vector<ReminderItem> items;
    bool grouped = false;
    bool startup = false;
};

[[nodiscard]] ReminderBatch evaluate_startup_reminders(
    const AppState& state,
    Clock::time_point now);

[[nodiscard]] ReminderBatch evaluate_tick_reminders(
    const AppState& state,
    Clock::time_point now);

[[nodiscard]] Task reset_reminder_if_schedule_changed(
    const Task& before,
    Task after);

}  // namespace desktop_todo
