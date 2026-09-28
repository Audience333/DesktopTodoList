#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace desktop_todo {

struct SelectionSnapshot {
    std::vector<std::wstring> selected_ids;
    std::optional<std::wstring> anchor_id;
    std::optional<std::wstring> focus_id;
};

class SelectionModel {
public:
    void select_one(std::wstring_view id);
    void toggle(std::wstring_view id);
    void select_ids(const std::vector<std::wstring>& ids, bool append);
    void select_range(
        std::wstring_view id,
        const std::vector<std::wstring>& visible_ids,
        bool append);
    void toggle_all_visible(const std::vector<std::wstring>& visible_ids);
    [[nodiscard]] std::optional<std::wstring> move_focus(
        const std::vector<std::wstring>& visible_ids,
        int offset,
        bool extend);
    void remove_missing(const std::vector<std::wstring>& existing_ids);
    [[nodiscard]] SelectionSnapshot snapshot() const;

private:
    std::set<std::wstring> selected_ids_;
    std::optional<std::wstring> anchor_id_;
    std::optional<std::wstring> focus_id_;
};

}  // namespace desktop_todo
