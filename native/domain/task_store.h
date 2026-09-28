#pragma once

#include "domain/commands.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace desktop_todo {

class TaskStore {
public:
    using Now = std::function<Clock::time_point()>;
    using NewId = std::function<std::wstring()>;

    TaskStore(AppState state, Now now, NewId new_id);

    [[nodiscard]] std::optional<Task> add_task(const AddTaskCommand& command);
    bool update_task(std::wstring_view id, const TaskPatch& patch);
    std::size_t update_tasks(const std::vector<std::wstring>& ids, const TaskPatch& patch);
    bool set_completed(std::wstring_view id, bool completed);
    std::size_t set_completed_tasks(const std::vector<std::wstring>& ids, bool completed);
    std::size_t delete_tasks(const std::vector<std::wstring>& ids);
    std::size_t clear_completed(bool confirmed);
    bool reorder(std::wstring_view id, std::wstring_view target_id, DropPosition position);
    bool mark_reminded(
        const std::vector<std::wstring>& ids,
        Clock::time_point delivered_at);

    [[nodiscard]] bool can_undo();
    bool undo();

    [[nodiscard]] const AppState& state() const noexcept;
    [[nodiscard]] StoreChange take_change();

private:
    struct UndoEntry {
        AppState state;
        Clock::time_point expires_at;
    };

    [[nodiscard]] std::vector<Task>::iterator find_task(std::wstring_view id);
    void save_undo();
    void expire_undo();
    void record_change(std::vector<std::wstring> affected_ids);

    AppState state_;
    Now now_;
    NewId new_id_;
    std::optional<UndoEntry> undo_;
    StoreChange pending_change_;
};

}  // namespace desktop_todo
