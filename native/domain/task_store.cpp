#include "domain/task_store.h"

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
    *found = std::move(*validated.task);
    record_change({found->id});
    return true;
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
    const auto source = find_task(id);
    const auto target = find_task(target_id);
    if (source == state_.tasks.end() || target == state_.tasks.end() || source == target) {
        return false;
    }

    save_undo();
    Task moved = std::move(*source);
    const auto target_index = static_cast<std::size_t>(target - state_.tasks.begin());
    const auto source_index = static_cast<std::size_t>(source - state_.tasks.begin());
    state_.tasks.erase(state_.tasks.begin() + static_cast<std::ptrdiff_t>(source_index));

    auto insertion_index = target_index;
    if (source_index < target_index) {
        --insertion_index;
    }
    if (position == DropPosition::after) {
        ++insertion_index;
    }
    state_.tasks.insert(
        state_.tasks.begin() + static_cast<std::ptrdiff_t>(insertion_index), std::move(moved));
    for (std::size_t index = 0; index < state_.tasks.size(); ++index) {
        state_.tasks[index].order = static_cast<double>(index + 1);
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
    state_ = std::move(undo_->state);
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
