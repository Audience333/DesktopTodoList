#include "application/app_service.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace desktop_todo {
namespace {

AppState merge_recovered_with_pending(AppState recovered, AppState pending) {
    for (auto& task : pending.tasks) {
        const auto existing = std::find_if(
            recovered.tasks.begin(), recovered.tasks.end(), [&task](const Task& candidate) {
                return candidate.id == task.id;
            });
        if (existing == recovered.tasks.end()) {
            recovered.tasks.push_back(std::move(task));
        } else if (task.updated_at > existing->updated_at) {
            *existing = std::move(task);
        }
    }
    return recovered;
}

}  // namespace

AppService::AppService(
    StateRepository& repository,
    TaskStore::Now now,
    TaskStore::NewId new_id,
    Today today,
    EventSink events)
    : repository_(repository), now_(std::move(now)), new_id_(std::move(new_id)),
      today_(std::move(today)), events_(std::move(events)) {}

bool AppService::start() {
    auto loaded = repository_.load();
    const auto load_status = loaded.status;
    const auto load_error = loaded.error;
    auto load_issues = loaded.issues;
    const auto preserved_source = loaded.preserved_source;
    writes_blocked_ = !loaded.writable;
    recovery_replacement_pending_ = false;
    recreate_store(std::move(loaded.state));
    const auto today = today_();
    if (!writes_blocked_) {
        const auto backup = repository_.ensure_daily_backup(store_->state(), today);
        if (backup.ok) last_backup_date_ = today;
        if (!backup.ok) emit({AppEventType::save_error, {}, {}, backup.error});
    }
    emit({AppEventType::started});
    if (load_status == LoadStatus::repaired || load_status == LoadStatus::restored ||
        load_status == LoadStatus::reset || !load_issues.empty()) {
        AppEvent recovery;
        recovery.type = AppEventType::recovery;
        recovery.message = load_error;
        recovery.load_status = load_status;
        recovery.issues = std::move(load_issues);
        recovery.preserved_source = preserved_source;
        emit(std::move(recovery));
    }
    auto startup = evaluate_startup_reminders(store_->state(), now_());
    if (!startup.items.empty()) emit({AppEventType::reminders, {}, std::move(startup)});
    return true;
}

std::optional<Task> AppService::add_task(const AddTaskCommand& command) {
    auto result = store_->add_task(command);
    collect_change();
    return result;
}

bool AppService::update_task(std::wstring_view id, const TaskPatch& patch) {
    const auto changed = store_->update_task(id, patch);
    collect_change();
    return changed;
}

std::size_t AppService::update_tasks(const std::vector<std::wstring>& ids, const TaskPatch& patch) {
    const auto changed = store_->update_tasks(ids, patch);
    collect_change();
    return changed;
}

bool AppService::set_completed(std::wstring_view id, bool completed) {
    const auto changed = store_->set_completed(id, completed);
    collect_change();
    return changed;
}

std::size_t AppService::set_completed_tasks(
    const std::vector<std::wstring>& ids,
    bool completed) {
    const auto changed = store_->set_completed_tasks(ids, completed);
    collect_change();
    return changed;
}

std::size_t AppService::delete_tasks(const std::vector<std::wstring>& ids) {
    const auto count = store_->delete_tasks(ids);
    prune_selection();
    collect_change();
    return count;
}

std::size_t AppService::clear_completed(bool confirmed) {
    const auto count = store_->clear_completed(confirmed);
    prune_selection();
    collect_change();
    return count;
}

bool AppService::reorder(
    std::wstring_view id,
    std::wstring_view target_id,
    DropPosition position) {
    const auto changed = store_->reorder(id, target_id, position);
    collect_change();
    return changed;
}

bool AppService::undo() {
    const auto changed = store_->undo();
    collect_change();
    return changed;
}

bool AppService::can_undo() {
    return store_->can_undo();
}

std::vector<TaskRef> AppService::query(const QuerySpec& query_spec) const {
    return query_tasks(store_->state(), query_spec, now_());
}

ImportResult AppService::prepare_import(
    std::span<const std::byte> source,
    ImportMode mode) const {
    return desktop_todo::prepare_import(store_->state(), source, mode);
}

bool AppService::accept_import(ImportResult result) {
    if (!result.candidate.has_value()) return false;
    if (result.mode == ImportMode::replace) {
        const auto backup = repository_.create_import_backup(store_->state());
        if (!backup.ok) {
            emit({AppEventType::save_error, {}, {}, backup.error});
            return false;
        }
    }
    recreate_store(std::move(*result.candidate));
    if (writes_blocked_ && result.mode == ImportMode::replace) {
        recovery_replacement_pending_ = true;
    }
    dirty_ = true;
    save_due_ = now_() + std::chrono::milliseconds{500};
    emit({AppEventType::state_changed});
    return true;
}

ExportResult AppService::export_to(const std::filesystem::path& destination) const {
    return repository_.export_to(destination, store_->state());
}

bool AppService::reset_to_defaults(bool confirmed) {
    if (!confirmed) return false;
    const auto backup = repository_.create_reset_backup(store_->state());
    if (!backup.ok) {
        emit({AppEventType::save_error, {}, {}, backup.error});
        return false;
    }
    recreate_store(AppState{});
    if (writes_blocked_) recovery_replacement_pending_ = true;
    dirty_ = true;
    save_due_ = now_() + std::chrono::milliseconds{500};
    emit({AppEventType::state_changed});
    return true;
}

ReminderBatch AppService::tick_reminders() {
    const auto timestamp = now_();
    auto batch = evaluate_tick_reminders(store_->state(), timestamp);
    if (!batch.items.empty()) {
        emit({AppEventType::reminders, {}, batch});
    }
    return batch;
}

bool AppService::acknowledge_reminders(const ReminderBatch& delivered) {
    std::vector<std::wstring> ids;
    for (const auto& item : delivered.items) {
        const auto current = std::find_if(
            store_->state().tasks.begin(), store_->state().tasks.end(),
            [&item](const Task& task) { return task.id == item.task_id; });
        if (current != store_->state().tasks.end() && current->remind &&
            current->due_at == item.due_at && !current->reminded_at.has_value()) {
            ids.push_back(item.task_id);
        }
    }
    const auto changed = store_->mark_reminded(ids, now_());
    collect_change();
    return changed;
}

bool AppService::maintenance() {
    bool success = true;
    if (writes_blocked_) {
        const auto preserved = repository_.preserve_corrupt_source();
        if (!preserved.ok) {
            emit({AppEventType::save_error, {}, {}, preserved.error});
            return false;
        }

        auto pending = store_->state();
        const auto had_pending_changes = dirty_;
        auto recovered = repository_.load();
        if (!recovered.writable) {
            emit({AppEventType::save_error, {}, {}, recovered.error});
            return false;
        }
        const auto recovery_status = recovered.status;
        const auto recovery_error = recovered.error;
        auto recovery_issues = recovered.issues;
        auto reconciled = recovery_replacement_pending_
            ? std::move(pending)
            : had_pending_changes
                ? merge_recovered_with_pending(std::move(recovered.state), std::move(pending))
                : std::move(recovered.state);
        recreate_store(std::move(reconciled));
        dirty_ = had_pending_changes || !recovery_error.empty();
        save_due_ = dirty_ ? std::optional<Clock::time_point>{now_()} : std::nullopt;
        writes_blocked_ = false;
        recovery_replacement_pending_ = false;

        AppEvent recovery_event;
        recovery_event.type = AppEventType::recovery;
        recovery_event.message = recovery_error;
        recovery_event.load_status = recovery_status;
        recovery_event.issues = std::move(recovery_issues);
        recovery_event.preserved_source = preserved.path;
        emit(std::move(recovery_event));
    }
    const auto today = today_();
    if (!last_backup_date_.has_value() || *last_backup_date_ != today) {
        const auto backup = repository_.ensure_daily_backup(store_->state(), today);
        if (!backup.ok) {
            emit({AppEventType::save_error, {}, {}, backup.error});
            success = false;
        } else {
            last_backup_date_ = today;
        }
    }
    if (dirty_ && save_due_.has_value() && now_() >= *save_due_ && !flush()) {
        success = false;
    }
    return success;
}

bool AppService::flush() {
    if (!dirty_) return true;
    if (writes_blocked_) {
        emit({AppEventType::save_error, {}, {},
            L"State writes are blocked until recovery source is preserved"});
        return false;
    }
    const auto saved = repository_.save(store_->state());
    if (!saved.ok) {
        emit({AppEventType::save_error, {}, {}, saved.error});
        return false;
    }
    dirty_ = false;
    save_due_.reset();
    return true;
}

const AppState& AppService::snapshot() const {
    if (!store_) throw std::logic_error{"AppService must be started first"};
    return store_->state();
}

SelectionModel& AppService::selection() noexcept { return selection_; }
const SelectionModel& AppService::selection() const noexcept { return selection_; }

void AppService::recreate_store(AppState state) {
    store_ = std::make_unique<TaskStore>(std::move(state), now_, new_id_);
    selection_ = SelectionModel{};
}

void AppService::collect_change() {
    const auto change = store_->take_change();
    if (!change.persisted) return;
    dirty_ = true;
    save_due_ = now_() + std::chrono::milliseconds{500};
    emit({AppEventType::state_changed, change.affected_ids});
}

void AppService::prune_selection() {
    std::vector<std::wstring> remaining;
    remaining.reserve(store_->state().tasks.size());
    for (const auto& task : store_->state().tasks) remaining.push_back(task.id);
    selection_.remove_missing(remaining);
}

void AppService::emit(AppEvent event) const {
    if (events_) events_(event);
}

}  // namespace desktop_todo
