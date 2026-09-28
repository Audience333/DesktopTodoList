#include "domain/selection_model.h"

#include <algorithm>
#include <unordered_set>

namespace desktop_todo {

void SelectionModel::select_one(std::wstring_view id) {
    const std::wstring value{id};
    if (selected_ids_.size() == 1 && selected_ids_.contains(value)) {
        selected_ids_.clear();
        anchor_id_.reset();
    } else {
        selected_ids_ = {value};
        anchor_id_ = value;
    }
    focus_id_ = value;
}

void SelectionModel::toggle(std::wstring_view id) {
    const std::wstring value{id};
    if (selected_ids_.contains(value)) {
        selected_ids_.erase(value);
    } else {
        selected_ids_.insert(value);
    }
    anchor_id_ = value;
    focus_id_ = value;
}

void SelectionModel::select_ids(const std::vector<std::wstring>& ids, bool append) {
    if (!append) selected_ids_.clear();
    selected_ids_.insert(ids.begin(), ids.end());
    if (ids.empty()) {
        if (!append) {
            anchor_id_.reset();
            focus_id_.reset();
        }
        return;
    }
    anchor_id_ = ids.front();
    focus_id_ = ids.back();
}

void SelectionModel::select_range(
    std::wstring_view id,
    const std::vector<std::wstring>& visible_ids,
    bool append) {
    const auto target = std::find(visible_ids.begin(), visible_ids.end(), id);
    const auto anchor = anchor_id_.has_value()
        ? std::find(visible_ids.begin(), visible_ids.end(), *anchor_id_)
        : visible_ids.end();
    if (target == visible_ids.end() || anchor == visible_ids.end()) {
        select_one(id);
        return;
    }

    if (!append) {
        selected_ids_.clear();
    }
    const auto target_index = static_cast<std::size_t>(target - visible_ids.begin());
    const auto anchor_index = static_cast<std::size_t>(anchor - visible_ids.begin());
    const auto first = std::min(target_index, anchor_index);
    const auto last = std::max(target_index, anchor_index);
    for (auto index = first; index <= last; ++index) {
        selected_ids_.insert(visible_ids[index]);
    }
    focus_id_ = std::wstring{id};
}

void SelectionModel::toggle_all_visible(const std::vector<std::wstring>& visible_ids) {
    if (visible_ids.empty()) {
        return;
    }
    const auto all_selected = std::all_of(
        visible_ids.begin(), visible_ids.end(), [this](const std::wstring& id) {
            return selected_ids_.contains(id);
        });
    if (all_selected) {
        for (const auto& id : visible_ids) {
            selected_ids_.erase(id);
        }
        if (anchor_id_.has_value() &&
            std::find(visible_ids.begin(), visible_ids.end(), *anchor_id_) != visible_ids.end()) {
            anchor_id_.reset();
        }
        if (focus_id_.has_value() &&
            std::find(visible_ids.begin(), visible_ids.end(), *focus_id_) != visible_ids.end()) {
            focus_id_.reset();
        }
        return;
    }

    selected_ids_.insert(visible_ids.begin(), visible_ids.end());
    anchor_id_ = visible_ids.front();
    focus_id_ = visible_ids.front();
}

std::optional<std::wstring> SelectionModel::move_focus(
    const std::vector<std::wstring>& visible_ids,
    int offset,
    bool extend) {
    if (visible_ids.empty()) {
        return std::nullopt;
    }

    auto current = focus_id_.has_value()
        ? std::find(visible_ids.begin(), visible_ids.end(), *focus_id_)
        : visible_ids.end();
    if (current == visible_ids.end()) {
        const auto& edge = offset < 0 ? visible_ids.back() : visible_ids.front();
        selected_ids_ = {edge};
        anchor_id_ = edge;
        focus_id_ = edge;
        return edge;
    }
    const auto current_index = static_cast<int>(current - visible_ids.begin());
    const auto maximum = static_cast<int>(visible_ids.size()) - 1;
    const auto next_index = std::clamp(current_index + offset, 0, maximum);
    const auto& next = visible_ids[static_cast<std::size_t>(next_index)];
    if (next == *focus_id_) {
        return next;
    }
    if (extend) {
        select_range(next, visible_ids, false);
    } else {
        select_one(next);
    }
    return next;
}

void SelectionModel::remove_missing(const std::vector<std::wstring>& existing_ids) {
    const std::unordered_set<std::wstring> existing(existing_ids.begin(), existing_ids.end());
    std::erase_if(selected_ids_, [&existing](const std::wstring& id) {
        return !existing.contains(id);
    });
    if (anchor_id_.has_value() && !existing.contains(*anchor_id_)) {
        anchor_id_.reset();
    }
    if (focus_id_.has_value() && !existing.contains(*focus_id_)) {
        focus_id_.reset();
    }
}

SelectionSnapshot SelectionModel::snapshot() const {
    return {
        std::vector<std::wstring>{selected_ids_.begin(), selected_ids_.end()},
        anchor_id_,
        focus_id_};
}

}  // namespace desktop_todo
