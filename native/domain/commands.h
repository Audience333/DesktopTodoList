#pragma once

#include "domain/types.h"

#include <optional>
#include <string>
#include <vector>

namespace desktop_todo {

struct AddTaskCommand {
    std::wstring title;
    std::wstring note;
    Priority priority = Priority::medium;
    std::optional<Clock::time_point> due_at;
    bool remind = true;
    std::vector<std::wstring> tags;
};

struct TaskPatch {
    std::optional<std::wstring> id;
    std::optional<std::wstring> title;
    std::optional<std::wstring> note;
    std::optional<Priority> priority;
    std::optional<std::optional<Clock::time_point>> due_at;
    std::optional<bool> remind;
    std::optional<std::vector<std::wstring>> tags;
    std::optional<Clock::time_point> created_at;
};

enum class DropPosition { before, after };

struct StoreChange {
    bool persisted = false;
    std::vector<std::wstring> affected_ids;
};

}  // namespace desktop_todo
