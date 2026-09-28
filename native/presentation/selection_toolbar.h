#pragma once

#include "domain/commands.h"
#include "presentation/layout.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace desktop_todo {

class AppService;

struct SelectionToolbarState {
    bool visible = false;
    std::size_t selected_count = 0;
    std::size_t hidden_count = 0;
};

enum class SelectionToolbarAction { complete, high_priority, edit_details, delete_selected, clear };

[[nodiscard]] SelectionToolbarState selection_toolbar_state(
    const std::vector<std::wstring>& selected_ids,
    const std::vector<std::wstring>& visible_ids);
[[nodiscard]] std::optional<SelectionToolbarAction> selection_toolbar_action_at(
    float x,
    float y,
    const RectF& bounds);

class SelectionToolbar {
public:
    explicit SelectionToolbar(AppService& service);
    void update(
        std::vector<std::wstring> selected_ids,
        const std::vector<std::wstring>& visible_ids);
    [[nodiscard]] const SelectionToolbarState& state() const noexcept;
    [[nodiscard]] std::size_t set_completed(bool completed);
    [[nodiscard]] std::size_t apply_patch(const TaskPatch& patch);
    [[nodiscard]] std::size_t delete_selected();
    void clear();

private:
    AppService& service_;
    std::vector<std::wstring> selected_ids_;
    SelectionToolbarState state_;
};

}  // namespace desktop_todo
