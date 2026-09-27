#include "domain/validation.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_set>
#include <unordered_map>

namespace desktop_todo {
namespace {

constexpr std::size_t max_title_length = 200;
constexpr std::size_t max_note_length = 2'000;
constexpr std::size_t max_tag_length = 24;
constexpr std::size_t max_tags = 5;

std::wstring trim(std::wstring value) {
    const auto not_space = [](wchar_t character) {
        return std::iswspace(static_cast<wint_t>(character)) == 0;
    };
    const auto first = std::find_if(value.begin(), value.end(), not_space);
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (first >= last) {
        return {};
    }
    return std::wstring(first, last);
}

void add_issue(std::vector<ValidationIssue>& issues, ValidationIssueCode code) {
    issues.push_back({code, std::nullopt});
}

bool is_valid(Priority value) {
    return value == Priority::low || value == Priority::medium || value == Priority::high;
}

bool is_valid(TaskStatus value) {
    return value == TaskStatus::todo || value == TaskStatus::done;
}

bool is_valid(Theme value) {
    return value == Theme::system || value == Theme::light || value == Theme::dark;
}

bool is_valid(ViewKind value) {
    return value == ViewKind::today || value == ViewKind::week ||
        value == ViewKind::all || value == ViewKind::done;
}

bool is_valid(WindowMode value) {
    return value == WindowMode::normal || value == WindowMode::floating;
}

bool is_valid(WindowLayer value) {
    return value == WindowLayer::top || value == WindowLayer::normal ||
        value == WindowLayer::bottom;
}

}  // namespace

TaskValidation validate_task(Task task, double fallback_order, Clock::time_point now) {
    TaskValidation result;

    task.title = trim(std::move(task.title));
    if (task.title.empty()) {
        add_issue(result.issues, ValidationIssueCode::blank_title);
        return result;
    }
    if (task.title.size() > max_title_length) {
        task.title.resize(max_title_length);
        add_issue(result.issues, ValidationIssueCode::title_truncated);
    }
    if (task.note.size() > max_note_length) {
        task.note.resize(max_note_length);
        add_issue(result.issues, ValidationIssueCode::note_truncated);
    }
    if (!is_valid(task.priority)) {
        task.priority = Priority::medium;
        add_issue(result.issues, ValidationIssueCode::invalid_priority);
    }
    if (!is_valid(task.status)) {
        task.status = TaskStatus::todo;
        add_issue(result.issues, ValidationIssueCode::invalid_status);
    }

    std::vector<std::wstring> tags;
    tags.reserve(std::min(task.tags.size(), max_tags));
    bool tags_repaired = false;
    for (auto tag : task.tags) {
        tag = trim(std::move(tag));
        if (tag.size() > max_tag_length) {
            tag.resize(max_tag_length);
            tags_repaired = true;
        }
        if (tag.empty() || std::find(tags.begin(), tags.end(), tag) != tags.end()) {
            tags_repaired = true;
            continue;
        }
        if (tags.size() == max_tags) {
            tags_repaired = true;
            break;
        }
        tags.push_back(std::move(tag));
    }
    if (tags.size() != task.tags.size()) {
        tags_repaired = true;
    }
    task.tags = std::move(tags);
    if (tags_repaired) {
        add_issue(result.issues, ValidationIssueCode::tag_repaired);
    }

    if (!std::isfinite(task.order)) {
        task.order = std::isfinite(fallback_order) ? fallback_order : 0.0;
        add_issue(result.issues, ValidationIssueCode::invalid_order);
    }
    if (task.id.empty()) {
        add_issue(result.issues, ValidationIssueCode::missing_id);
        return result;
    }
    if (task.created_at == Clock::time_point{}) {
        task.created_at = now;
        add_issue(result.issues, ValidationIssueCode::created_at_repaired);
    }
    if (task.updated_at == Clock::time_point{}) {
        task.updated_at = task.created_at;
        add_issue(result.issues, ValidationIssueCode::updated_at_repaired);
    }
    if (task.status == TaskStatus::done && !task.completed_at.has_value()) {
        task.completed_at = now;
        add_issue(result.issues, ValidationIssueCode::completed_at_repaired);
    } else if (task.status == TaskStatus::todo && task.completed_at.has_value()) {
        task.completed_at.reset();
        add_issue(result.issues, ValidationIssueCode::completed_at_repaired);
    }

    result.task = std::move(task);
    return result;
}

StateValidation validate_state(AppState state, Clock::time_point now) {
    StateValidation result;
    result.state = std::move(state);
    if (result.state.schema_version != 1) {
        result.supported = false;
        add_issue(result.issues, ValidationIssueCode::unsupported_schema);
        return result;
    }

    auto& settings = result.state.settings;
    if (!is_valid(settings.theme)) {
        settings.theme = Theme::system;
        add_issue(result.issues, ValidationIssueCode::invalid_theme);
    }
    if (!is_valid(settings.default_filter)) {
        settings.default_filter = ViewKind::today;
        add_issue(result.issues, ValidationIssueCode::invalid_default_filter);
    }
    if (!is_valid(settings.window_mode)) {
        settings.window_mode = WindowMode::normal;
        add_issue(result.issues, ValidationIssueCode::invalid_window_mode);
    }
    if (!is_valid(settings.window_layer)) {
        settings.window_layer = WindowLayer::normal;
        add_issue(result.issues, ValidationIssueCode::invalid_window_layer);
    }
    if (settings.week_starts_on != 0 && settings.week_starts_on != 1) {
        settings.week_starts_on = 1;
        add_issue(result.issues, ValidationIssueCode::invalid_week_start);
    }
    if (settings.remind_advance_minutes < 0) {
        settings.remind_advance_minutes = 0;
        add_issue(result.issues, ValidationIssueCode::invalid_reminder_advance);
    }
    auto& geometry = settings.floating_geometry;
    if (!std::isfinite(geometry.width) || geometry.width < 260.0 || geometry.width > 2'000.0 ||
        !std::isfinite(geometry.height) || geometry.height < 240.0 || geometry.height > 2'000.0) {
        geometry = FloatingGeometry{};
        add_issue(result.issues, ValidationIssueCode::invalid_geometry);
    }

    std::vector<Task> validated_tasks;
    validated_tasks.reserve(result.state.tasks.size());
    std::unordered_map<std::wstring, std::size_t> ids;
    for (std::size_t index = 0; index < result.state.tasks.size(); ++index) {
        auto validated = validate_task(result.state.tasks[index], static_cast<double>(index + 1), now);
        for (auto issue : validated.issues) {
            issue.task_index = index;
            result.issues.push_back(issue);
        }
        if (!validated.task.has_value()) {
            ++result.dropped_tasks;
            continue;
        }
        const auto existing = ids.find(validated.task->id);
        if (existing != ids.end()) {
            result.issues.push_back({ValidationIssueCode::duplicate_id, index});
            if (validated.task->updated_at > validated_tasks[existing->second].updated_at) {
                validated_tasks[existing->second] = std::move(*validated.task);
            }
            ++result.dropped_tasks;
            continue;
        }
        ids.emplace(validated.task->id, validated_tasks.size());
        validated_tasks.push_back(std::move(*validated.task));
    }
    result.state.tasks = std::move(validated_tasks);
    return result;
}

}  // namespace desktop_todo
