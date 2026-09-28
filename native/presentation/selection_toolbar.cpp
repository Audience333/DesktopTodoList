#include "presentation/selection_toolbar.h"

#include "application/app_service.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace desktop_todo {

SelectionToolbarState selection_toolbar_state(
    const std::vector<std::wstring>& selected_ids,
    const std::vector<std::wstring>& visible_ids) {
    const auto visible_count = static_cast<std::size_t>(std::count_if(
        selected_ids.begin(), selected_ids.end(), [&visible_ids](const std::wstring& id) {
            return std::find(visible_ids.begin(), visible_ids.end(), id) != visible_ids.end();
        }));
    return {!selected_ids.empty(), selected_ids.size(), selected_ids.size() - visible_count};
}

std::optional<SelectionToolbarAction> selection_toolbar_action_at(
    float x,
    float y,
    const RectF& bounds) {
    if (x < bounds.x || x >= bounds.right() || y < bounds.y || y >= bounds.bottom())
        return std::nullopt;
    constexpr float gap = 3.0F;
    constexpr SelectionToolbarAction actions[]{
        SelectionToolbarAction::complete,
        SelectionToolbarAction::high_priority,
        SelectionToolbarAction::edit_details,
        SelectionToolbarAction::delete_selected,
        SelectionToolbarAction::clear};
    const auto label_width = std::min(86.0F, std::max(58.0F, bounds.width * 0.26F));
    const auto button_width = (bounds.width - label_width - gap * 5.0F) / 5.0F;
    const auto start = bounds.x + label_width + gap;
    if (x < start || button_width <= 0) return std::nullopt;
    const auto index = static_cast<std::size_t>((x - start) / (button_width + gap));
    if (index >= std::size(actions)) return std::nullopt;
    const auto left = start + static_cast<float>(index) * (button_width + gap);
    if (x >= left + button_width) return std::nullopt;
    return actions[index];
}

SelectionToolbar::SelectionToolbar(AppService& service) : service_(service) {}

void SelectionToolbar::update(
    std::vector<std::wstring> selected_ids,
    const std::vector<std::wstring>& visible_ids) {
    selected_ids_ = std::move(selected_ids);
    state_ = selection_toolbar_state(selected_ids_, visible_ids);
}

const SelectionToolbarState& SelectionToolbar::state() const noexcept { return state_; }

std::size_t SelectionToolbar::set_completed(bool completed) {
    return service_.set_completed_tasks(selected_ids_, completed);
}

std::size_t SelectionToolbar::apply_patch(const TaskPatch& patch) {
    return service_.update_tasks(selected_ids_, patch);
}

std::size_t SelectionToolbar::delete_selected() {
    const auto deleted = service_.delete_tasks(selected_ids_);
    selected_ids_.clear();
    state_ = {};
    return deleted;
}

void SelectionToolbar::clear() {
    service_.selection().select_ids({}, false);
    selected_ids_.clear();
    state_ = {};
}

}  // namespace desktop_todo
