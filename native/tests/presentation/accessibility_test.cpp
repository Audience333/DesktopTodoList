#include "presentation/accessibility_provider.h"

#include "tests/test_support.h"

#include <string>
#include <vector>

using namespace desktop_todo;

TEST_CASE(accessibility_provider_exposes_logical_focus_order_names_and_roles) {
    const auto tree = build_accessibility_tree({
        .window_name = L"DesktopTodoList",
        .task_rows = {{L"task-a", L"买牛奶", false, true}, {L"task-b", L"交报告", true, false}},
        .focused_id = L"search"});

    EXPECT_EQ(tree.children.front().id, L"new-task");
    EXPECT_EQ(tree.children.front().role, AccessibleRole::button);
    EXPECT_EQ(tree.children[1].id, L"search");
    EXPECT_TRUE(tree.children[1].focused);
    EXPECT_EQ(tree.children[2].role, AccessibleRole::tab);
    EXPECT_EQ(tree.children[6].id, L"task-a");
    EXPECT_EQ(tree.children[6].role, AccessibleRole::list_item);
    EXPECT_TRUE(tree.children[6].selected);
}

TEST_CASE(accessibility_provider_keeps_virtual_task_identity_stable_across_reordering) {
    const auto first = build_accessibility_tree({
        .task_rows = {{L"task-a", L"A", false, false}, {L"task-b", L"B", false, false}}});
    const auto reordered = build_accessibility_tree({
        .task_rows = {{L"task-b", L"B", false, false}, {L"task-a", L"A", false, false}}});

    EXPECT_EQ(first.children[6].id, L"task-a");
    EXPECT_EQ(reordered.children[7].id, L"task-a");
    EXPECT_EQ(first.children[6].runtime_id, reordered.children[7].runtime_id);
}

