#pragma once

#include "domain/types.h"

#include <string>
#include <vector>

namespace desktop_todo {

struct QuerySpec {
    ViewKind view = ViewKind::all;
    std::wstring search;
    int week_starts_on = 1;
    bool include_completed = false;
};

struct TaskRef {
    const Task* task = nullptr;
};

[[nodiscard]] std::vector<TaskRef> query_tasks(
    const AppState& state,
    const QuerySpec& query,
    Clock::time_point now);

void compact_order(std::vector<Task>& tasks);

}  // namespace desktop_todo
