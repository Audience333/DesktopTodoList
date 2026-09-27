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
    const auto load_status = loaded.status;
    const auto load_error = loaded.error;
    auto load_issues = loaded.issues;
    const auto preserved_source = loaded.preserved_source;
    recreate_store(std::move(loaded.state));
    const auto today = today_();
    const auto backup = repository_.ensure_daily_backup(store_->state(), today);
    if (backup.ok) last_backup_date_ = today;
    if (!backup.ok) emit({AppEventType::save_error, {}, {}, backup.error});
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

bool AppService::set_completed(std::wstring_view id, bool completed) {
    const auto changed = store_->set_completed(id, completed);
    collect_change();
    return changed;
}

std::size_t AppService::delete_tasks(const std::vector<std::wstring>& ids) {
    const auto count = store_->delete_tasks(ids);
    collect_change();
    prune_selection();
    return count;
}

std::size_t AppService::clear_completed(bool confirmed) {
    const auto count = store_->clear_completed(confirmed);
    collect_change();
    prune_selection();
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

bool AppService::acknowledge_reminders(const std::vector<std::wstring>& ids) {
    const auto changed = store_->mark_reminded(ids, now_());
    collect_change();
    return changed;
}

bool AppService::maintenance() {
    const auto today = today_();
    if (!last_backup_date_.has_value() || *last_backup_date_ != today) {
        const auto backup = repository_.ensure_daily_backup(store_->state(), today);
        if (!backup.ok) {
            emit({AppEventType::save_error, {}, {}, backup.error});
            return false;
        }
        last_backup_date_ = today;
    }
    if (dirty_ && save_due_.has_value() && now_() >= *save_due_) return flush();
    return true;
}

bool AppService::flush() {
    if (!dirty_) return true;
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
