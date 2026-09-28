#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "domain/types.h"
#include "presentation/layout.h"

#include <functional>
#include <string>
#include <vector>

namespace desktop_todo {

enum class AccessibleRole { window, button, edit, tab, list_item };

struct AccessibleTaskRow {
    std::wstring id;
    std::wstring title;
    bool completed = false;
    bool selected = false;
    RectF bounds;
};

struct AccessibilityTreeInput {
    std::wstring window_name = L"DesktopTodoList";
    std::vector<AccessibleTaskRow> task_rows;
    std::wstring focused_id;
    ViewKind current_view = ViewKind::today;
};

struct AccessibleNode {
    std::wstring id;
    std::wstring runtime_id;
    std::wstring name;
    AccessibleRole role = AccessibleRole::button;
    bool enabled = true;
    bool focused = false;
    bool selected = false;
    bool checked = false;
    RectF bounds;
    std::vector<AccessibleNode> children;
};

using AccessibilityTree = AccessibleNode;

[[nodiscard]] AccessibilityTree build_accessibility_tree(const AccessibilityTreeInput& input);
[[nodiscard]] std::vector<std::wstring> accessibility_focus_order(const AccessibilityTree& tree);

class AccessibilityProvider {
public:
    using Snapshot = std::function<AccessibilityTree()>;

    AccessibilityProvider(HWND window, Snapshot snapshot);
    [[nodiscard]] LRESULT handle_get_object(WPARAM wparam, LPARAM lparam) const;

private:
    HWND window_ = nullptr;
    Snapshot snapshot_;
};

}  // namespace desktop_todo
