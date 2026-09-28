#pragma once

#include "domain/reminder_engine.h"
#include "domain/selection_model.h"
#include "domain/task_query.h"
#include "domain/task_store.h"
#include "persistence/import_export.h"
#include "persistence/state_repository.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace desktop_todo {

enum class AppEventType { started, state_changed, reminders, recovery, save_error };

struct AppEvent {
    AppEventType type = AppEventType::state_changed;
    std::vector<std::wstring> affected_ids;
    ReminderBatch reminder_batch;
    std::wstring message;
    std::optional<LoadStatus> load_status;
    std::vector<ValidationIssue> issues;
    std::filesystem::path preserved_source;
};

class AppService {
public:
    using Today = std::function<LocalDate()>;
    using EventSink = std::function<void(const AppEvent&)>;

    AppService(
        StateRepository& repository,
        TaskStore::Now now,
        TaskStore::NewId new_id,
        Today today,
        EventSink events = {});

    [[nodiscard]] bool start();
    [[nodiscard]] std::optional<Task> add_task(const AddTaskCommand& command);
    bool update_task(std::wstring_view id, const TaskPatch& patch);
    std::size_t update_tasks(const std::vector<std::wstring>& ids, const TaskPatch& patch);
    bool set_completed(std::wstring_view id, bool completed);
    std::size_t set_completed_tasks(const std::vector<std::wstring>& ids, bool completed);
    std::size_t delete_tasks(const std::vector<std::wstring>& ids);
    std::size_t clear_completed(bool confirmed);
    bool reorder(std::wstring_view id, std::wstring_view target_id, DropPosition position);
    bool undo();
    [[nodiscard]] bool can_undo();

    [[nodiscard]] std::vector<TaskRef> query(const QuerySpec& query) const;
    [[nodiscard]] ImportResult prepare_import(
        std::span<const std::byte> source,
        ImportMode mode) const;
    bool accept_import(ImportResult result);
    bool set_close_to_tray(bool enabled);
    bool set_window_layer(WindowLayer layer);
    bool set_selectable(bool enabled);
    bool update_settings(Settings settings);
    [[nodiscard]] ExportResult export_to(const std::filesystem::path& destination) const;
    bool reset_to_defaults(bool confirmed);
    [[nodiscard]] ReminderBatch tick_reminders();
    bool acknowledge_reminders(const ReminderBatch& delivered);
    bool maintenance();
    bool flush();

    [[nodiscard]] const AppState& snapshot() const;
    [[nodiscard]] SelectionModel& selection() noexcept;
    [[nodiscard]] const SelectionModel& selection() const noexcept;

private:
    void recreate_store(AppState state);
    void collect_change();
    void prune_selection();
    void emit(AppEvent event) const;

    StateRepository& repository_;
    TaskStore::Now now_;
    TaskStore::NewId new_id_;
    Today today_;
    EventSink events_;
    std::unique_ptr<TaskStore> store_;
    SelectionModel selection_;
    bool dirty_ = false;
    std::optional<Clock::time_point> save_due_;
    std::optional<LocalDate> last_backup_date_;
    bool writes_blocked_ = false;
    bool recovery_replacement_pending_ = false;
};

}  // namespace desktop_todo
