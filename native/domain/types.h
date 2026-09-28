#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace desktop_todo {

struct Clock {
    using time_point = std::chrono::sys_time<std::chrono::milliseconds>;
};

enum class Priority { low, medium, high };
enum class TaskStatus { todo, done };
enum class ViewKind { today, week, all, done };
enum class Theme { system, light, dark };
enum class WindowMode { normal, floating };
enum class WindowLayer { top, normal, bottom };

struct FloatingGeometry {
    std::optional<double> x;
    std::optional<double> y;
    double width = 360.0;
    double height = 480.0;
};

struct Task {
    std::wstring id;
    std::wstring title;
    std::wstring note;
    Priority priority = Priority::medium;
    TaskStatus status = TaskStatus::todo;
    std::optional<Clock::time_point> due_at;
    bool remind = true;
    std::optional<Clock::time_point> reminded_at;
    std::vector<std::wstring> tags;
    double order = 0.0;
    Clock::time_point created_at{};
    Clock::time_point updated_at{};
    std::optional<Clock::time_point> completed_at;
};

struct Settings {
    Theme theme = Theme::system;
    ViewKind default_filter = ViewKind::today;
    std::wstring hotkey = L"Ctrl+Alt+T";
    std::wstring selectable_hotkey = L"Ctrl+Alt+L";
    int week_starts_on = 1;
    int remind_advance_minutes = 0;
    bool auto_start = false;
    bool start_minimized = false;
    bool close_to_tray = true;
    int click_through_timeout_minutes = 0;
    WindowMode window_mode = WindowMode::normal;
    WindowLayer window_layer = WindowLayer::normal;
    bool selectable = true;
    FloatingGeometry floating_geometry;
    bool multi_select_enabled = true;
    bool rubber_band_select = true;
    bool keep_selection_across_views = true;
};

struct AppState {
    int schema_version = 1;
    std::vector<Task> tasks;
    Settings settings;
};

enum class ValidationIssueCode {
    unsupported_schema,
    blank_title,
    title_truncated,
    note_truncated,
    tag_repaired,
    invalid_priority,
    invalid_status,
    invalid_order,
    missing_id,
    invalid_theme,
    invalid_default_filter,
    invalid_window_mode,
    invalid_window_layer,
    invalid_click_through_timeout,
    invalid_week_start,
    invalid_reminder_advance,
    invalid_geometry,
    duplicate_id,
    completed_at_repaired,
    created_at_repaired,
    updated_at_repaired
};

struct ValidationIssue {
    ValidationIssueCode code;
    std::optional<std::size_t> task_index;
};

struct TaskValidation {
    std::optional<Task> task;
    std::vector<ValidationIssue> issues;
};

struct StateValidation {
    AppState state;
    std::vector<ValidationIssue> issues;
    bool supported = true;
    std::size_t dropped_tasks = 0;
};

}  // namespace desktop_todo
