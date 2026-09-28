#include "domain/task_store.h"

#include "domain/reminder_engine.h"
#include "domain/validation.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <utility>

namespace desktop_todo {
namespace {

constexpr auto undo_window = std::chrono::seconds{5};

std::vector<std::wstring> task_ids(const AppState& state) {
    std::vector<std::wstring> ids;
    ids.reserve(state.tasks.size());
    for (const auto& task : state.tasks) {
        ids.push_back(task.id);
    }
    return ids;
}

}  // namespace

TaskStore::TaskStore(AppState state, Now now, NewId new_id)
    : state_(std::move(state)), now_(std::move(now)), new_id_(std::move(new_id)) {}

std::optional<Task> TaskStore::add_task(const AddTaskCommand& command) {
    const auto timestamp = now_();
    const auto minimum_order = std::min_element(
        state_.tasks.begin(), state_.tasks.end(), [](const Task& left, const Task& right) {
            return left.order < right.order;
        });
    const double order = minimum_order == state_.tasks.end() ? 1.0 : minimum_order->order - 1.0;

    Task candidate;
    candidate.id = new_id_();
    candidate.title = command.title;
    candidate.note = command.note;
    candidate.priority = command.priority;
    candidate.due_at = command.due_at;
    candidate.remind = command.remind;
    candidate.tags = command.tags;
    candidate.order = order;
    candidate.created_at = timestamp;
    candidate.updated_at = timestamp;

    auto validated = validate_task(std::move(candidate), order, timestamp);
    if (!validated.task.has_value()) {
        return std::nullopt;
    }

    undo_.reset();
    state_.tasks.push_back(*validated.task);
    record_change({validated.task->id});
    return validated.task;
}

bool TaskStore::update_task(std::wstring_view id, const TaskPatch& patch) {
    const auto found = find_task(id);
    if (found == state_.tasks.end()) {
        return false;
    }

    Task candidate = *found;
    if (patch.title.has_value()) {
        candidate.title = *patch.title;
    }
    if (patch.note.has_value()) {
        candidate.note = *patch.note;
    }
    if (patch.priority.has_value()) {
        candidate.priority = *patch.priority;
    }
    if (patch.due_at.has_value()) {
        candidate.due_at = *patch.due_at;
    }
    if (patch.remind.has_value()) {
        candidate.remind = *patch.remind;
    }
    if (patch.tags.has_value()) {
        candidate.tags = *patch.tags;
    }
    const auto timestamp = now_();
    candidate.updated_at = timestamp;

    auto validated = validate_task(std::move(candidate), found->order, timestamp);
    if (!validated.task.has_value()) {
        return false;
    }

    save_undo();
    *found = reset_reminder_if_schedule_changed(*found, std::move(*validated.task));
    record_change({found->id});
    return true;
}

std::size_t TaskStore::update_tasks(
    const std::vector<std::wstring>& ids,
    const TaskPatch& patch) {
    const std::unordered_set<std::wstring> requested(ids.begin(), ids.end());
    std::vector<std::pair<Task*, Task>> replacements;
    replacements.reserve(requested.size());
    const auto timestamp = now_();
    for (auto& current : state_.tasks) {
        if (!requested.contains(current.id)) continue;
        Task candidate = current;
        if (patch.title) candidate.title = *patch.title;
        if (patch.note) candidate.note = *patch.note;
        if (patch.priority) candidate.priority = *patch.priority;
        if (patch.due_at) candidate.due_at = *patch.due_at;
        if (patch.remind) candidate.remind = *patch.remind;
        if (patch.tags) candidate.tags = *patch.tags;
        candidate.updated_at = timestamp;
        auto validated = validate_task(std::move(candidate), current.order, timestamp);
        if (!validated.task) return 0;
        replacements.emplace_back(&current, std::move(*validated.task));
    }
    if (replacements.empty()) return 0;
    save_undo();
    std::vector<std::wstring> changed_ids;
    changed_ids.reserve(replacements.size());
    for (auto& [current, replacement] : replacements) {
        replacement = reset_reminder_if_schedule_changed(*current, std::move(replacement));
        changed_ids.push_back(current->id);
        *current = std::move(replacement);
    }
    record_change(std::move(changed_ids));
    return replacements.size();
}

bool TaskStore::set_completed(std::wstring_view id, bool completed) {
    const auto found = find_task(id);
    if (found == state_.tasks.end()) {
        return false;
    }
    const auto target_status = completed ? TaskStatus::done : TaskStatus::todo;
    if (found->status == target_status) {
        return false;
    }

    save_undo();
    const auto timestamp = now_();
    found->status = target_status;
    found->completed_at = completed ? std::optional{timestamp} : std::nullopt;
    found->updated_at = timestamp;
    record_change({found->id});
    return true;
}

std::size_t TaskStore::set_completed_tasks(
    const std::vector<std::wstring>& ids,
    bool completed) {
    const std::unordered_set<std::wstring> requested(ids.begin(), ids.end());
    const auto target_status = completed ? TaskStatus::done : TaskStatus::todo;
    std::vector<Task*> changed;
    for (auto& task : state_.tasks) {
        if (requested.contains(task.id) && task.status != target_status) changed.push_back(&task);
    }
    if (changed.empty()) return 0;
    save_undo();
    const auto timestamp = now_();
    std::vector<std::wstring> changed_ids;
    changed_ids.reserve(changed.size());
    for (auto* task : changed) {
        task->status = target_status;
        task->completed_at = completed ? std::optional{timestamp} : std::nullopt;
        task->updated_at = timestamp;
        changed_ids.push_back(task->id);
    }
    record_change(std::move(changed_ids));
    return changed.size();
}

std::size_t TaskStore::delete_tasks(const std::vector<std::wstring>& ids) {
    const std::unordered_set<std::wstring> requested(ids.begin(), ids.end());
    std::vector<std::wstring> deleted;
    for (const auto& task : state_.tasks) {
        if (requested.contains(task.id)) {
            deleted.push_back(task.id);
        }
    }
    if (deleted.empty()) {
        return 0;
    }

    save_undo();
    std::erase_if(state_.tasks, [&requested](const Task& task) {
        return requested.contains(task.id);
    });
    record_change(deleted);
    return deleted.size();
}

std::size_t TaskStore::clear_completed(bool confirmed) {
    if (!confirmed) {
        return 0;
    }

    std::vector<std::wstring> completed_ids;
    for (const auto& task : state_.tasks) {
        if (task.status == TaskStatus::done) {
            completed_ids.push_back(task.id);
        }
    }
    return delete_tasks(completed_ids);
}

bool TaskStore::reorder(
    std::wstring_view id,
    std::wstring_view target_id,
    DropPosition position) {
    if (id == target_id || find_task(id) == state_.tasks.end() ||
        find_task(target_id) == state_.tasks.end()) {
        return false;
    }

    save_undo();
    std::vector<Task*> ordered;
    ordered.reserve(state_.tasks.size());
    for (auto& task : state_.tasks) ordered.push_back(&task);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Task* left, const Task* right) {
        if (left->order != right->order) return left->order < right->order;
        return left->id < right->id;
    });
    const auto source = std::find_if(ordered.begin(), ordered.end(), [id](const Task* task) {
        return task->id == id;
    });
    auto moved = *source;
    ordered.erase(source);
    const auto target = std::find_if(ordered.begin(), ordered.end(), [target_id](const Task* task) {
        return task->id == target_id;
    });
    auto insertion = target;
    if (position == DropPosition::after) ++insertion;
    ordered.insert(insertion, moved);
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        ordered[index]->order = static_cast<double>(index + 1);
    }
    record_change(task_ids(state_));
    return true;
}

bool TaskStore::mark_reminded(
    const std::vector<std::wstring>& ids,
    Clock::time_point delivered_at) {
    const std::unordered_set<std::wstring> requested(ids.begin(), ids.end());
    std::vector<std::wstring> changed;
    for (auto& task : state_.tasks) {
        if (requested.contains(task.id) && task.reminded_at != delivered_at) {
            task.reminded_at = delivered_at;
            task.updated_at = delivered_at;
            changed.push_back(task.id);
        }
    }
    if (changed.empty()) return false;
    record_change(std::move(changed));
    return true;
}

bool TaskStore::set_close_to_tray(bool enabled) {
    if (state_.settings.close_to_tray == enabled) return false;
    state_.settings.close_to_tray = enabled;
    record_change({});
    return true;
}

bool TaskStore::set_window_layer(WindowLayer layer) {
    if (state_.settings.window_layer == layer) return false;
    state_.settings.window_layer = layer;
    record_change({});
    return true;
}

bool TaskStore::set_selectable(bool enabled) {
    if (state_.settings.selectable == enabled) return false;
    state_.settings.selectable = enabled;
    record_change({});
    return true;
}

bool TaskStore::can_undo() {
    expire_undo();
    return undo_.has_value();
}

bool TaskStore::undo() {
    expire_undo();
    if (!undo_.has_value()) {
        return false;
    }

    auto affected = task_ids(state_);
    const auto restored_ids = task_ids(undo_->state);
    for (const auto& id : restored_ids) {
        if (std::find(affected.begin(), affected.end(), id) == affected.end()) {
            affected.push_back(id);
        }
    }
    auto restored = std::move(undo_->state);
    for (auto& restored_task : restored.tasks) {
        const auto current = std::find_if(
            state_.tasks.begin(), state_.tasks.end(), [&restored_task](const Task& task) {
                return task.id == restored_task.id;
            });
        if (current != state_.tasks.end() && current->due_at == restored_task.due_at &&
            current->remind == restored_task.remind && current->reminded_at.has_value()) {
            restored_task.reminded_at = current->reminded_at;
            restored_task.updated_at = std::max(restored_task.updated_at, current->updated_at);
        }
    }
    state_ = std::move(restored);
    undo_.reset();
    record_change(std::move(affected));
    return true;
}

const AppState& TaskStore::state() const noexcept {
    return state_;
}

StoreChange TaskStore::take_change() {
    return std::exchange(pending_change_, StoreChange{});
}

std::vector<Task>::iterator TaskStore::find_task(std::wstring_view id) {
    return std::find_if(state_.tasks.begin(), state_.tasks.end(), [id](const Task& task) {
        return task.id == id;
    });
}

void TaskStore::save_undo() {
    undo_ = UndoEntry{state_, now_() + undo_window};
}

void TaskStore::expire_undo() {
    if (undo_.has_value() && now_() >= undo_->expires_at) {
        undo_.reset();
    }
}

void TaskStore::record_change(std::vector<std::wstring> affected_ids) {
    pending_change_.persisted = true;
    pending_change_.affected_ids = std::move(affected_ids);
}

}  // namespace desktop_todo
