#pragma once

#include "domain/task_query.h"

#include <cstddef>
#include <string>
#include <vector>

namespace desktop_todo {

enum class HighlightField { title, note, tag };

struct HighlightSpan {
    HighlightField field = HighlightField::title;
    std::size_t index = 0;
    std::size_t start = 0;
    std::size_t length = 0;
    bool operator==(const HighlightSpan&) const = default;
};

struct TaskRowModel {
    std::wstring id;
    std::wstring title;
    std::wstring note;
    Priority priority = Priority::medium;
    TaskStatus status = TaskStatus::todo;
    std::optional<Clock::time_point> due_at;
    bool overdue = false;
    std::vector<HighlightSpan> highlights;
};

struct ViewCounts {
    std::size_t today = 0;
    std::size_t week = 0;
    std::size_t all = 0;
    std::size_t done = 0;
    std::size_t overdue = 0;
};

struct ViewModel {
    std::vector<TaskRowModel> rows;
    ViewCounts counts;
};

[[nodiscard]] ViewModel build_view_model(
    const AppState& state,
    const QuerySpec& query,
    Clock::time_point now);

}  // namespace desktop_todo
