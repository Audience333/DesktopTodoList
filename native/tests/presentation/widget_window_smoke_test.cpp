#include "test_support.h"

#include "application/app_service.h"
#include "persistence/fake_file_system.h"
#include "presentation/details_panel.h"
#include "presentation/layout.h"
#include "presentation/task_list_view.h"
#include "presentation/text_editor.h"
#include "presentation/widget_window.h"

#include <algorithm>
#include <string_view>

using desktop_todo::AppService;
using desktop_todo::Clock;
using desktop_todo::LocalDate;
using desktop_todo::StateRepository;
using desktop_todo::test_support::FakeFileSystem;

namespace {

void drain_messages() {
    MSG message{};
    std::size_t count = 0;
    while (PeekMessageW(&message, nullptr, 0, WM_TIMER - 1, PM_REMOVE) ||
        PeekMessageW(&message, nullptr, WM_TIMER + 1, 0xFFFF, PM_REMOVE)) {
        if (++count > 1000) throw std::runtime_error("message queue did not drain");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

POINT client_point(const desktop_todo::PointF point, UINT dpi) {
    const auto scale = static_cast<float>(dpi) / 96.0F;
    return {static_cast<LONG>(point.x * scale), static_cast<LONG>(point.y * scale)};
}

void click_widget(HWND widget, UINT message, POINT point) {
    if (message == WM_LBUTTONUP) {
        SendMessageW(widget, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    }
    SendMessageW(widget, message, 0, MAKELPARAM(point.x, point.y));
}

}  // namespace

TEST_CASE(widget_window_smoke_creates_and_destroys_integrated_native_widget) {
    FakeFileSystem files;
    StateRepository repository{files, L"C:/widget-smoke", [] { return L"test"; }};
    const auto now = [] { return Clock::time_point{std::chrono::milliseconds{1'800'000'000'000}}; };
    std::size_t next_id = 0;
    desktop_todo::WidgetWindow* widget_pointer = nullptr;
    AppService service{repository, now, [&next_id] {
            return L"new-id-" + std::to_wstring(++next_id);
        },
        [] { return LocalDate{2026, 9, 28}; },
        [&widget_pointer](const desktop_todo::AppEvent&) {
            if (widget_pointer != nullptr) widget_pointer->invalidate();
        }};
    EXPECT_TRUE(service.start());
    const auto task_by_id = [&service](std::wstring_view id) -> const desktop_todo::Task* {
        const auto& tasks = service.snapshot().tasks;
        const auto found = std::find_if(tasks.begin(), tasks.end(), [id](const auto& task) {
            return task.id == id;
        });
        return found == tasks.end() ? nullptr : &*found;
    };

    desktop_todo::WidgetWindow widget{service};
    widget_pointer = &widget;
    EXPECT_TRUE(widget.create(GetModuleHandleW(nullptr), SW_HIDE));
    EXPECT_TRUE(widget.handle() != nullptr);
    EXPECT_TRUE(widget.toggle_visibility());
    UpdateWindow(widget.handle());
    EXPECT_TRUE(IsWindowVisible(widget.handle()) != FALSE);
    SendMessageW(widget.handle(), WM_CLOSE, 0, 0);
    EXPECT_TRUE(IsWindow(widget.handle()) != FALSE);
    EXPECT_TRUE(IsWindowVisible(widget.handle()) == FALSE);
    EXPECT_TRUE(widget.toggle_visibility());
    EXPECT_TRUE(IsWindowVisible(widget.handle()) != FALSE);
    PostMessageW(widget.handle(), desktop_todo::kEditorNewTaskMessage, 0, 0);
    drain_messages();
    const auto quick_add = GetDlgItem(widget.handle(), 501);
    const auto inline_title = GetDlgItem(widget.handle(), 502);
    const auto search = GetDlgItem(widget.handle(), 503);
    EXPECT_TRUE(quick_add != nullptr);
    EXPECT_TRUE(inline_title != nullptr);
    EXPECT_TRUE(search != nullptr);
    EXPECT_TRUE(GetFocus() == quick_add);

    SendMessageW(quick_add, WM_KEYDOWN, VK_RETURN, 0);
    drain_messages();
    EXPECT_TRUE(service.snapshot().tasks.empty());
    EXPECT_TRUE(GetFocus() == quick_add);

    SetWindowTextW(quick_add, L"集成测试任务");
    SendMessageW(quick_add, WM_KEYDOWN, VK_RETURN, 0);
    drain_messages();
    EXPECT_EQ(service.snapshot().tasks.size(), std::size_t{1});
    EXPECT_EQ(service.snapshot().tasks.front().title, L"集成测试任务");
    EXPECT_TRUE(GetFocus() == quick_add);
    EXPECT_EQ(GetWindowTextLengthW(quick_add), 0);

    PostMessageW(widget.handle(), desktop_todo::kEditorSearchMessage, 0, 0);
    drain_messages();
    EXPECT_TRUE(GetFocus() == search);
    SendMessageW(search, WM_KEYDOWN, VK_ESCAPE, 0);
    drain_messages();
    EXPECT_EQ(service.snapshot().tasks.size(), std::size_t{1});

    RECT client{};
    GetClientRect(widget.handle(), &client);
    const auto dpi = GetDpiForWindow(widget.handle());
    const auto layout = desktop_todo::calculate_layout(
        {static_cast<float>(client.right), static_cast<float>(client.bottom)},
        static_cast<float>(dpi),
        desktop_todo::LayoutMode::compact);
    const auto tab_width = layout.tabs.width / 4.0F;
    const auto all_tab = client_point(
        {layout.tabs.x + tab_width * 2.5F, layout.tabs.y + 10}, dpi);
    click_widget(widget.handle(), WM_LBUTTONUP, all_tab);
    UpdateWindow(widget.handle());
    const auto other_task = service.add_task({.title = L"另一行"});
    EXPECT_TRUE(other_task.has_value());
    UpdateWindow(widget.handle());
    RECT window_rect{};
    GetWindowRect(widget.handle(), &window_rect);
    SetWindowPos(widget.handle(), nullptr, window_rect.left, window_rect.top,
        window_rect.right - window_rect.left, static_cast<int>(320.0F * dpi / 96.0F),
        SWP_NOACTIVATE | SWP_NOZORDER);
    UpdateWindow(widget.handle());
    RECT compact_client{};
    GetClientRect(widget.handle(), &compact_client);
    auto compact_layout = desktop_todo::calculate_layout(
        {static_cast<float>(compact_client.right), static_cast<float>(compact_client.bottom)},
        static_cast<float>(dpi), desktop_todo::LayoutMode::compact);
    const auto title_point = client_point(
        {compact_layout.task_list.x + 60, compact_layout.task_list.y + 20}, dpi);
    const auto lower_title_point = client_point(
        {compact_layout.task_list.x + 60, compact_layout.task_list.y + 78}, dpi);

    click_widget(widget.handle(), WM_LBUTTONUP, lower_title_point);
    EXPECT_TRUE(GetFocus() == inline_title);
    SetWindowTextW(inline_title, L"旧会话草稿");
    SetFocus(widget.handle());
    click_widget(widget.handle(), WM_LBUTTONUP, title_point);
    drain_messages();
    EXPECT_TRUE(GetFocus() == inline_title);
    EXPECT_TRUE(task_by_id(L"new-id-2") != nullptr);
    EXPECT_EQ(task_by_id(L"new-id-1")->title, L"旧会话草稿");
    EXPECT_EQ(task_by_id(L"new-id-2")->title, L"另一行");
    SendMessageW(inline_title, WM_KEYDOWN, VK_ESCAPE, 0);
    drain_messages();

    click_widget(widget.handle(), WM_LBUTTONUP, title_point);
    EXPECT_TRUE(GetFocus() == inline_title);
    EXPECT_TRUE(service.selection().snapshot().selected_ids.empty());
    SetWindowTextW(inline_title, L"回车已保存");
    SendMessageW(inline_title, WM_KEYDOWN, VK_RETURN, 0);
    drain_messages();
    EXPECT_EQ(task_by_id(L"new-id-2")->title, L"回车已保存");

    click_widget(widget.handle(), WM_LBUTTONUP, title_point);
    SetWindowTextW(inline_title, L"Esc 不应保存");
    SendMessageW(inline_title, WM_KEYDOWN, VK_ESCAPE, 0);
    drain_messages();
    EXPECT_EQ(task_by_id(L"new-id-2")->title, L"回车已保存");

    click_widget(widget.handle(), WM_LBUTTONUP, title_point);
    SetWindowTextW(inline_title, L"失焦已保存");
    SetFocus(widget.handle());
    drain_messages();
    EXPECT_EQ(task_by_id(L"new-id-2")->title, L"失焦已保存");

    click_widget(widget.handle(), WM_LBUTTONDBLCLK, title_point);
    RECT expanded_client{};
    GetClientRect(widget.handle(), &expanded_client);
    const auto expanded_layout = desktop_todo::calculate_layout(
        {static_cast<float>(expanded_client.right), static_cast<float>(expanded_client.bottom)},
        static_cast<float>(dpi), desktop_todo::LayoutMode::compact);
    EXPECT_TRUE(expanded_layout.task_list.height >= 259.0F);
    const auto note = GetDlgItem(widget.handle(), 532);
    const auto priority = GetDlgItem(widget.handle(), 534);
    const auto due = GetDlgItem(widget.handle(), 536);
    const auto tags = GetDlgItem(widget.handle(), 538);
    const auto remind = GetDlgItem(widget.handle(), 539);
    const auto save = GetDlgItem(widget.handle(), desktop_todo::kDetailsSaveControlId);
    EXPECT_TRUE(note != nullptr && priority != nullptr && due != nullptr &&
        tags != nullptr && remind != nullptr && save != nullptr);
    SetWindowTextW(note, L"中文备注 🐈\r\n第二行");
    SendMessageW(priority, CB_SETCURSEL, 2, 0);
    SetWindowTextW(due, L"2026-12-31 18:45");
    SetWindowTextW(tags, L"工作, 发布 🚀");
    SendMessageW(remind, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(widget.handle(), WM_COMMAND,
        MAKEWPARAM(desktop_todo::kDetailsSaveControlId, BN_CLICKED),
        reinterpret_cast<LPARAM>(save));
    EXPECT_EQ(task_by_id(L"new-id-2")->note, L"中文备注 🐈\r\n第二行");
    EXPECT_EQ(task_by_id(L"new-id-2")->priority, desktop_todo::Priority::high);
    EXPECT_TRUE(task_by_id(L"new-id-2")->due_at.has_value());
    EXPECT_TRUE(!task_by_id(L"new-id-2")->remind);
    EXPECT_EQ(task_by_id(L"new-id-2")->tags.size(), std::size_t{2});

    RECT batch_client{};
    GetClientRect(widget.handle(), &batch_client);
    const auto batch_layout = desktop_todo::calculate_layout(
        {static_cast<float>(batch_client.right), static_cast<float>(batch_client.bottom)},
        static_cast<float>(dpi), desktop_todo::LayoutMode::compact);
    const auto top_id = task_by_id(L"new-id-1")->order < task_by_id(L"new-id-2")->order
        ? std::wstring{L"new-id-1"} : std::wstring{L"new-id-2"};
    const auto bottom_id = top_id == L"new-id-1" ? std::wstring{L"new-id-2"} :
        std::wstring{L"new-id-1"};
    const auto drag_start = client_point({batch_layout.task_list.right() - 14,
        batch_layout.task_list.y + 26}, dpi);
    const auto drag_target = client_point({batch_layout.task_list.right() - 14,
        batch_layout.task_list.y + 58 + 46}, dpi);
    SendMessageW(widget.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(drag_start.x, drag_start.y));
    EXPECT_TRUE(GetCapture() == widget.handle());
    SendMessageW(widget.handle(), WM_MOUSEMOVE, MK_LBUTTON,
        MAKELPARAM(drag_target.x, drag_target.y));
    SendMessageW(widget.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(drag_target.x, drag_target.y));
    EXPECT_TRUE(GetCapture() != widget.handle());
    EXPECT_TRUE(task_by_id(top_id)->order > task_by_id(bottom_id)->order);

    const auto band_start = client_point({batch_layout.task_list.x + 4,
        batch_layout.task_list.y + 2 * 58.0F + 12}, dpi);
    const auto band_end = client_point({batch_layout.task_list.right() - 4,
        batch_layout.task_list.y + 8}, dpi);
    SendMessageW(widget.handle(), WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(band_start.x, band_start.y));
    SendMessageW(widget.handle(), WM_MOUSEMOVE, MK_LBUTTON,
        MAKELPARAM(band_end.x, band_end.y));
    EXPECT_TRUE(GetCapture() == widget.handle());
    SendMessageW(widget.handle(), WM_LBUTTONUP, 0,
        MAKELPARAM(band_end.x, band_end.y));
    EXPECT_EQ(service.selection().snapshot().selected_ids.size(), std::size_t{2});
    EXPECT_TRUE(GetCapture() != widget.handle());

    const auto toolbar_click = [&widget, dpi](std::size_t action_index) {
        RECT client{};
        GetClientRect(widget.handle(), &client);
        const auto layout = desktop_todo::calculate_layout(
            {static_cast<float>(client.right), static_cast<float>(client.bottom)},
            static_cast<float>(dpi), desktop_todo::LayoutMode::compact);
        const auto label_width = std::min(86.0F, std::max(58.0F, layout.footer.width * 0.26F));
        constexpr float gap = 3.0F;
        const auto button_width = (layout.footer.width - label_width - gap * 5.0F) / 5.0F;
        const auto x = layout.footer.x + label_width + gap +
            static_cast<float>(action_index) * (button_width + gap) + button_width / 2;
        click_widget(widget.handle(), WM_LBUTTONUP,
            client_point({x, layout.footer.y + 14}, dpi));
        UpdateWindow(widget.handle());
    };

    toolbar_click(1);
    EXPECT_EQ(task_by_id(L"new-id-1")->priority, desktop_todo::Priority::high);
    EXPECT_EQ(task_by_id(L"new-id-2")->priority, desktop_todo::Priority::high);
    toolbar_click(0);
    EXPECT_EQ(task_by_id(L"new-id-1")->status, desktop_todo::TaskStatus::done);
    EXPECT_EQ(task_by_id(L"new-id-2")->status, desktop_todo::TaskStatus::done);

    toolbar_click(2);
    EXPECT_TRUE(GetDlgItem(widget.handle(), 532) != nullptr);
    SetWindowTextW(GetDlgItem(widget.handle(), 532), L"批量备注");
    SendMessageW(GetDlgItem(widget.handle(), 534), CB_SETCURSEL, 0, 0);
    SetWindowTextW(GetDlgItem(widget.handle(), 536), L"2026-12-31 18:45");
    SetWindowTextW(GetDlgItem(widget.handle(), 538), L"批量,共享");
    SendMessageW(widget.handle(), WM_COMMAND,
        MAKEWPARAM(desktop_todo::kDetailsSaveControlId, BN_CLICKED),
        reinterpret_cast<LPARAM>(GetDlgItem(widget.handle(),
            desktop_todo::kDetailsSaveControlId)));
    EXPECT_EQ(task_by_id(L"new-id-1")->note, L"批量备注");
    EXPECT_EQ(task_by_id(L"new-id-2")->note, L"批量备注");
    EXPECT_EQ(task_by_id(L"new-id-1")->priority, desktop_todo::Priority::low);
    EXPECT_EQ(task_by_id(L"new-id-2")->priority, desktop_todo::Priority::low);
    EXPECT_TRUE(task_by_id(L"new-id-1")->due_at.has_value());
    EXPECT_TRUE(task_by_id(L"new-id-2")->due_at.has_value());
    EXPECT_EQ(task_by_id(L"new-id-1")->tags.size(), std::size_t{2});
    EXPECT_EQ(task_by_id(L"new-id-2")->tags.size(), std::size_t{2});

    toolbar_click(3);
    EXPECT_TRUE(service.snapshot().tasks.empty());
    EXPECT_TRUE(service.undo());
    EXPECT_EQ(service.snapshot().tasks.size(), std::size_t{2});
    EXPECT_TRUE(!service.undo());

    widget.destroy();
    EXPECT_TRUE(widget.handle() == nullptr);
}
