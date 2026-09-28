#include "domain/task_query.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>

namespace desktop_todo {
namespace {

constexpr double minimum_order_gap = 1e-6;
constexpr double maximum_order_magnitude = 1e6;

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return value;
}

bool matches_search(const Task& task, const std::wstring& search) {
    if (search.empty()) {
        return true;
    }
    const auto contains = [&search](const std::wstring& value) {
        return lowercase(value).find(search) != std::wstring::npos;
    };
    if (contains(task.title) || contains(task.note)) {
        return true;
    }
    return std::any_of(task.tags.begin(), task.tags.end(), contains);
}

std::chrono::sys_days day_of(Clock::time_point value, std::chrono::minutes utc_offset) {
    return std::chrono::floor<std::chrono::days>(value + utc_offset);
}

std::chrono::sys_days local_day_of(Clock::time_point value, const QuerySpec& query) {
    return query.local_day ? query.local_day(value) : day_of(value, query.utc_offset);
}

std::pair<std::chrono::sys_days, std::chrono::sys_days> week_range(
    std::chrono::sys_days today,
    int week_starts_on) {
    const auto weekday = std::chrono::weekday{today}.c_encoding();
    const auto start_day = week_starts_on == 0 ? 0U : 1U;
    const auto offset = (weekday + 7U - start_day) % 7U;
    const auto start = today - std::chrono::days{offset};
    return {start, start + std::chrono::days{7}};
}

bool is_overdue(const Task& task, Clock::time_point now) {
    return task.status == TaskStatus::todo && task.due_at.has_value() && *task.due_at < now;
}

bool matches_view(
    const Task& task,
    const QuerySpec& query,
    Clock::time_point now) {
    if (query.view == ViewKind::done) {
        return task.status == TaskStatus::done;
    }
    if (task.status == TaskStatus::done) {
        return query.view == ViewKind::all && query.include_completed;
    }
    if (query.view == ViewKind::all) {
        return true;
    }
    if (!task.due_at.has_value()) {
        return false;
    }
    if (query.view == ViewKind::today) {
        return *task.due_at < now ||
            local_day_of(*task.due_at, query) == local_day_of(now, query);
    }
    if (query.view == ViewKind::week) {
        if (*task.due_at < now) {
            return true;
        }
        const auto [start, end] = week_range(local_day_of(now, query), query.week_starts_on);
        const auto due_day = local_day_of(*task.due_at, query);
        return due_day >= start && due_day < end;
    }
    return false;
}

int priority_rank(Priority priority) {
    switch (priority) {
    case Priority::high:
        return 0;
    case Priority::medium:
        return 1;
    case Priority::low:
        return 2;
    }
    return 1;
}

Clock::time_point due_or_max(const Task& task) {
    return task.due_at.value_or(Clock::time_point::max());
}

}  // namespace

std::vector<TaskRef> query_tasks(
    const AppState& state,
    const QuerySpec& query,
    Clock::time_point now) {
    const auto search = lowercase(query.search);
    std::vector<TaskRef> result;
    result.reserve(state.tasks.size());
    for (const auto& task : state.tasks) {
        if (matches_view(task, query, now) && matches_search(task, search)) {
            result.push_back({&task});
        }
    }

    std::stable_sort(result.begin(), result.end(), [now](TaskRef left, TaskRef right) {
        const auto& a = *left.task;
        const auto& b = *right.task;
        if (a.status != b.status) {
            return a.status == TaskStatus::todo;
        }
        const auto a_overdue = is_overdue(a, now);
        const auto b_overdue = is_overdue(b, now);
        if (a_overdue != b_overdue) {
            return a_overdue;
        }
        if (priority_rank(a.priority) != priority_rank(b.priority)) {
            return priority_rank(a.priority) < priority_rank(b.priority);
        }
        if (due_or_max(a) != due_or_max(b)) {
            return due_or_max(a) < due_or_max(b);
        }
        if (a.status == TaskStatus::done && a.completed_at != b.completed_at) {
            return a.completed_at.value_or(Clock::time_point{}) >
                b.completed_at.value_or(Clock::time_point{});
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.id < b.id;
    });
    return result;
}

void compact_order(std::vector<Task>& tasks) {
    std::vector<Task*> ordered;
    ordered.reserve(tasks.size());
    for (auto& task : tasks) ordered.push_back(&task);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Task* left, const Task* right) {
        if (left->order != right->order) return left->order < right->order;
        return left->id < right->id;
    });
    bool compact = false;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const auto order = ordered[index]->order;
        if (!std::isfinite(order) || std::abs(order) > maximum_order_magnitude) {
            compact = true;
            break;
        }
        if (index > 0 && std::abs(order - ordered[index - 1]->order) < minimum_order_gap) {
            compact = true;
            break;
        }
    }
    if (!compact) {
        return;
    }
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        ordered[index]->order = static_cast<double>(index + 1);
    }
}

}  // namespace desktop_todo
