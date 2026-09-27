#include "application/app_service.h"

#include <stdexcept>
#include <utility>

namespace desktop_todo {

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
    recreate_store(std::move(loaded.state));
    const auto backup = repository_.ensure_daily_backup(store_->state(), today_());
    if (!backup.ok) emit({AppEventType::save_error, {}, {}, backup.error});
    emit({AppEventType::started});
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

bool AppService::set_completed(std::wstring_view id, bool completed) {
    const auto changed = store_->set_completed(id, completed);
    collect_change();
    return changed;
}

std::size_t AppService::delete_tasks(const std::vector<std::wstring>& ids) {
    const auto count = store_->delete_tasks(ids);
    collect_change();
    std::vector<std::wstring> remaining;
    remaining.reserve(store_->state().tasks.size());
    for (const auto& task : store_->state().tasks) remaining.push_back(task.id);
    selection_.remove_missing(remaining);
    return count;
}

std::size_t AppService::clear_completed(bool confirmed) {
    const auto count = store_->clear_completed(confirmed);
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
    recreate_store(std::move(*result.candidate));
    dirty_ = true;
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
    dirty_ = true;
    emit({AppEventType::state_changed});
    return true;
}

ReminderBatch AppService::tick_reminders() {
    const auto timestamp = now_();
    auto batch = evaluate_tick_reminders(store_->state(), timestamp);
    if (!batch.items.empty()) {
        std::vector<std::wstring> ids;
        ids.reserve(batch.items.size());
        for (const auto& item : batch.items) ids.push_back(item.task_id);
        store_->mark_reminded(ids, timestamp);
        collect_change();
        emit({AppEventType::reminders, {}, batch});
    }
    return batch;
}

bool AppService::flush() {
    if (!dirty_) return true;
    const auto saved = repository_.save(store_->state());
    if (!saved.ok) {
        emit({AppEventType::save_error, {}, {}, saved.error});
        return false;
    }
    dirty_ = false;
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
    emit({AppEventType::state_changed, change.affected_ids});
}

void AppService::emit(AppEvent event) const {
    if (events_) events_(event);
}

}  // namespace desktop_todo
