#include "test_support.h"

#include "platform/windows/window_class.h"
#include "presentation/details_panel.h"
#include "presentation/renderer.h"
#include "presentation/text_editor.h"


namespace {

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

TEST_CASE(editor_host_smoke_creates_native_input_and_details_controls) {
    const auto instance = GetModuleHandleW(nullptr);
    desktop_todo::WindowClass window_class{
        instance, L"DesktopTodoList.EditorHostSmoke", &host_proc, nullptr};
    EXPECT_TRUE(window_class.registered());
    const auto parent = CreateWindowExW(0, window_class.name(), L"", WS_POPUP,
        0, 0, 360, 480, nullptr, nullptr, instance, nullptr);
    EXPECT_TRUE(parent != nullptr);

    desktop_todo::TextEditor editor;
    EXPECT_TRUE(editor.create(parent, 96));
    EXPECT_TRUE(GetDlgItem(parent, 501) != nullptr);
    EXPECT_TRUE(GetDlgItem(parent, 502) != nullptr);
    EXPECT_TRUE(GetDlgItem(parent, 503) != nullptr);
    editor.layout(desktop_todo::calculate_layout({360, 480}, 96,
        desktop_todo::LayoutMode::compact), 96);

    desktop_todo::DetailsPanel details;
    EXPECT_TRUE(details.create(parent));
    EXPECT_TRUE(GetDlgItem(parent, 534) != nullptr);
    desktop_todo::Task task;
    task.id = L"smoke-task";
    task.title = L"Smoke";
    details.open(task, {16, 152, 328, 268}, 96);
    EXPECT_TRUE(details.visible());
    EXPECT_EQ(details.task_id(), L"smoke-task");

    desktop_todo::Renderer renderer{parent};
    EXPECT_TRUE(renderer.create_device_resources());
    const auto layout = desktop_todo::calculate_layout({360, 480}, 96,
        desktop_todo::LayoutMode::compact);
    const auto palette = desktop_todo::resolve_theme(desktop_todo::Theme::system, {});
    renderer.draw(layout, palette, {}, desktop_todo::ViewKind::today, {}, 0, true);
    ShowWindow(parent, SW_SHOWNORMAL);
    UpdateWindow(parent);
    renderer.draw(layout, palette, {}, desktop_todo::ViewKind::today, {}, 0, true);
    ShowWindow(parent, SW_HIDE);

    details.close();
    details.destroy();
    editor.destroy();
    DestroyWindow(parent);
}
