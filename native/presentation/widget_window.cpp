#include "presentation/widget_window.h"

#include "application/app_service.h"
#include "domain/commands.h"
#include "platform/windows/window_class.h"
#include "platform/windows/tray_icon.h"
#include "presentation/details_panel.h"
#include "presentation/layout.h"
#include "presentation/renderer.h"
#include "presentation/task_list_view.h"
#include "presentation/text_editor.h"
#include "presentation/theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <windowsx.h>

namespace desktop_todo {
namespace {

constexpr wchar_t kWidgetWindowClass[] = L"DesktopTodoList.WidgetWindow.v2";
constexpr UINT_PTR kMaintenanceTimer = 1;
constexpr float kDetailsWindowMinimumHeight = 500.0F;

SystemTheme read_system_theme() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    DWORD light_theme = 1;
    DWORD light_theme_size = sizeof(light_theme);
    const auto theme_status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
        &light_theme, &light_theme_size);
    return {
        .dark = theme_status == ERROR_SUCCESS && light_theme == 0,
        .high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0,
        .reduced_motion = animations == FALSE};
}

Clock::time_point now() {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
}

std::chrono::sys_days local_day(Clock::time_point instant) {
    constexpr std::int64_t windows_epoch_offset_ms = 11'644'473'600'000LL;
    const auto ticks = static_cast<ULONGLONG>(
        instant.time_since_epoch().count() + windows_epoch_offset_ms) * 10'000ULL;
    ULARGE_INTEGER value{};
    value.QuadPart = ticks;
    FILETIME utc_file_time{value.LowPart, value.HighPart};
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&utc_file_time, &utc) ||
        !SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) {
        return std::chrono::floor<std::chrono::days>(instant);
    }
    return std::chrono::sys_days{
        std::chrono::year{local.wYear} / local.wMonth / local.wDay};
}

LayoutResult current_layout(HWND window, UINT dpi) {
    RECT client{};
    GetClientRect(window, &client);
    return calculate_layout(
        {static_cast<float>(client.right), static_cast<float>(client.bottom)},
        static_cast<float>(dpi), LayoutMode::compact);
}

}  // namespace

WidgetWindow::WidgetWindow(AppService& service) : service_(service) {}

WidgetWindow::~WidgetWindow() { destroy(); }

bool WidgetWindow::create(HINSTANCE instance, int show_command) {
    window_class_ = std::make_unique<WindowClass>(
        instance, kWidgetWindowClass, &WidgetWindow::window_proc, nullptr);
    if (!window_class_->registered()) return false;

    dpi_ = GetDpiForSystem();
    const auto size = default_widget_size(static_cast<float>(dpi_));
    POINT origin{};
    MONITORINFO monitor_info{sizeof(monitor_info)};
    int x = 0;
    int y = 0;
    const auto primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(primary, &monitor_info)) {
        x = std::max(monitor_info.rcWork.left,
            monitor_info.rcWork.right - static_cast<int>(size.width) - 16);
        y = std::max(monitor_info.rcWork.top,
            monitor_info.rcWork.bottom - static_cast<int>(size.height) - 16);
    }
    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        window_class_->name(), L"DesktopTodoList", WS_POPUP | WS_THICKFRAME,
        x, y,
        static_cast<int>(size.width), static_cast<int>(size.height),
        nullptr, nullptr, instance, this);
    if (window_ == nullptr) return false;
    dpi_ = GetDpiForWindow(window_);
    renderer_ = std::make_unique<Renderer>(window_);
    pointer_controller_ = std::make_unique<PointerController>(service_.selection());
    selection_toolbar_ = std::make_unique<SelectionToolbar>(service_);
    text_editor_ = std::make_unique<TextEditor>();
    if (!text_editor_->create(window_, dpi_)) return false;
    details_panel_ = std::make_unique<DetailsPanel>();
    if (!details_panel_->create(window_)) return false;
    text_editor_->layout(current_layout(window_, dpi_), dpi_);
    clamp_to_monitor();
    SetTimer(window_, kMaintenanceTimer, 250, nullptr);
    ShowWindow(window_, show_command);
    UpdateWindow(window_);
    return true;
}

void WidgetWindow::destroy() {
    cancel_pointer_gesture();
    if (text_editor_) text_editor_->destroy();
    text_editor_.reset();
    pointer_controller_.reset();
    selection_toolbar_.reset();
    if (details_panel_) details_panel_->destroy();
    details_panel_.reset();
    if (window_ != nullptr) DestroyWindow(window_);
    renderer_.reset();
    window_class_.reset();
}

void WidgetWindow::invalidate() {
    if (window_ != nullptr) InvalidateRect(window_, nullptr, FALSE);
}

void WidgetWindow::show_and_activate() {
    if (window_ == nullptr) return;
    if (IsIconic(window_)) ShowWindow(window_, SW_RESTORE);
    ShowWindow(window_, SW_SHOWNORMAL);
    if (!SetForegroundWindow(window_)) {
        FLASHWINFO flash{sizeof(flash), window_, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&flash);
    }
}

void WidgetWindow::hide_to_tray() {
    if (window_ != nullptr) ShowWindow(window_, SW_HIDE);
}

bool WidgetWindow::toggle_visibility() {
    if (window_ == nullptr) return false;
    if (IsWindowVisible(window_)) {
        hide_to_tray();
        return false;
    }
    show_and_activate();
    return true;
}

void WidgetWindow::begin_new_task() {
    if (text_editor_) text_editor_->begin_new_task();
}

HWND WidgetWindow::handle() const noexcept { return window_; }

LRESULT CALLBACK WidgetWindow::window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    WidgetWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<WidgetWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self != nullptr
        ? self->handle_message(message, wparam, lparam)
        : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT WidgetWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wparam != 0) return 0;
        break;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        return hit_test(point);
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = static_cast<LONG>(
            minimum_widget_width(static_cast<float>(dpi_)));
        const auto minimum_height = details_panel_ && details_panel_->visible()
            ? kDetailsWindowMinimumHeight : 320.0F;
        limits->ptMinTrackSize.y = static_cast<LONG>(minimum_height * dpi_ / 96.0F);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        invalidate();
        return 0;
    }
    case WM_DISPLAYCHANGE:
        clamp_to_monitor();
        return 0;
    case WM_SIZE:
        if (renderer_) renderer_->resize(LOWORD(lparam), HIWORD(lparam));
        if (text_editor_) text_editor_->layout(current_layout(window_, dpi_), dpi_);
        if (details_panel_ && details_panel_->visible())
            details_panel_->layout(current_layout(window_, dpi_).task_list, dpi_);
        invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        handle_pointer_down({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;
    case WM_MOUSEMOVE:
        if ((wparam & MK_LBUTTON) != 0)
            handle_pointer_move({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;
    case WM_LBUTTONUP:
        handle_pointer_up({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return 0;
    case WM_CAPTURECHANGED:
        if (pointer_controller_ && pointer_controller_->active()) {
            pointer_controller_->cancel();
            selection_band_.reset();
            invalidate();
        }
        return 0;
    case WM_KILLFOCUS:
        cancel_pointer_gesture();
        return 0;
    case WM_LBUTTONDBLCLK: {
        const auto scale = static_cast<float>(dpi_) / 96.0F;
        const PointF logical{
            static_cast<float>(GET_X_LPARAM(lparam)) / scale,
            static_cast<float>(GET_Y_LPARAM(lparam)) / scale};
        const auto layout = current_layout(window_, dpi_);
        const auto local_y = logical.y - layout.task_list.y + scroll_y_;
        if (local_y >= 0) {
            const auto index = static_cast<std::size_t>(local_y / 58.0F);
            if (index < view_model_.rows.size()) {
                const auto& id = view_model_.rows[index].id;
                const auto& tasks = service_.snapshot().tasks;
                const auto task = std::find_if(tasks.begin(), tasks.end(),
                    [&id](const Task& value) { return value.id == id; });
                if (task != tasks.end() && details_panel_) {
                    details_task_id_ = id;
                    details_panel_->open(*task, layout.task_list, dpi_);
                    ensure_details_window_height();
                    invalidate();
                }
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        const auto child = reinterpret_cast<HWND>(lparam);
        if (details_panel_ && HIWORD(wparam) == BN_CLICKED &&
            LOWORD(wparam) == kDetailsSaveControlId) {
            const auto patch = details_panel_->read_patch();
            if (!patch) {
                MessageBeep(MB_ICONWARNING);
                return 0;
            }
            const bool changed = patch->note.has_value() || patch->priority.has_value() ||
                patch->due_at.has_value() || patch->remind.has_value() || patch->tags.has_value();
            if (changed) {
                if (details_batch_)
                    static_cast<void>(selection_toolbar_->apply_patch(*patch));
                else
                    static_cast<void>(service_.update_task(details_panel_->task_id(), *patch));
            }
            details_panel_->close();
            details_task_id_.clear();
            details_batch_ = false;
            SetFocus(window_);
            invalidate();
            return 0;
        }
        if (details_panel_ && HIWORD(wparam) == BN_CLICKED &&
            LOWORD(wparam) == kDetailsCancelControlId) {
            details_panel_->close();
            details_task_id_.clear();
            details_batch_ = false;
            SetFocus(window_);
            invalidate();
            return 0;
        }
        if (text_editor_ && HIWORD(wparam) == EN_CHANGE) {
            text_editor_->capture_text(child);
            if (text_editor_->control_for(child) == NativeEditorControl::search) {
                search_text_ = text_editor_->search_text();
            }
            invalidate();
            return 0;
        }
        break;
    }
    case kEditorCommitMessage:
        if (text_editor_) {
            const auto mode = text_editor_->active_control();
            const auto result = text_editor_->commit_active();
            if (result && mode == NativeEditorControl::quick_add) {
                if (!service_.add_task(AddTaskCommand{.title = result->text}).has_value()) {
                    MessageBeep(MB_ICONWARNING);
                }
                invalidate();
                if (text_editor_->active_handle() != nullptr) SetFocus(text_editor_->active_handle());
            } else if (result && mode == NativeEditorControl::inline_title) {
                TaskPatch patch;
                patch.title = result->text;
                static_cast<void>(service_.update_task(result->target_id, patch));
                invalidate();
            } else if (result && mode == NativeEditorControl::search) {
                search_text_ = result->text;
                invalidate();
            } else if (!result) {
                MessageBeep(MB_ICONWARNING);
            }
        }
        return 0;
    case kEditorCancelMessage:
    case kDetailsCancelMessage:
        if (text_editor_) {
            if (details_panel_ && details_panel_->visible()) {
                details_panel_->close();
                details_task_id_.clear();
                details_batch_ = false;
                SetFocus(window_);
            } else {
                text_editor_->cancel_active();
                search_text_ = text_editor_->search_text();
            }
            invalidate();
        }
        return 0;
    case kEditorFocusLostMessage:
        if (text_editor_ && text_editor_->active_control() == NativeEditorControl::inline_title &&
            text_editor_->active_handle() == reinterpret_cast<HWND>(lparam) &&
            text_editor_->focus_generation() == static_cast<std::uint32_t>(wparam)) {
            commit_inline_title();
        }
        return 0;
    case kEditorNewTaskMessage:
    case kEditorSearchMessage:
    case kEditorMoveTaskMessage:
    case WM_KEYDOWN:
        if (text_editor_ && (message == kEditorNewTaskMessage ||
            (wparam == 'N' && (GetKeyState(VK_CONTROL) & 0x8000) != 0))) {
            text_editor_->begin_new_task();
            return 0;
        }
        if (text_editor_ && (message == kEditorSearchMessage ||
            (wparam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) != 0))) {
            text_editor_->focus_search();
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE && text_editor_) {
            if (pointer_controller_ && pointer_controller_->active()) {
                cancel_pointer_gesture();
                invalidate();
                return 0;
            }
            if (details_panel_ && details_panel_->visible()) {
                details_panel_->close();
                details_task_id_.clear();
                SetFocus(window_);
                invalidate();
                return 0;
            }
            if (text_editor_->active_control() != NativeEditorControl::none) {
                text_editor_->cancel_active();
            } else if (!search_text_.empty()) {
                text_editor_->clear_search();
                search_text_.clear();
                invalidate();
            }
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_DELETE) {
            static_cast<void>(service_.delete_tasks(service_.selection().snapshot().selected_ids));
            invalidate();
            return 0;
        }
        if (message == WM_KEYDOWN && (wparam == VK_UP || wparam == VK_DOWN) &&
            text_editor_ && text_editor_->active_control() == NativeEditorControl::none) {
            const auto focused = service_.selection().move_focus(visible_task_ids(),
                wparam == VK_UP ? -1 : 1, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (focused) invalidate();
            return 0;
        }
        if (message == kEditorMoveTaskMessage ||
            (message == WM_KEYDOWN && (GetKeyState(VK_MENU) & 0x8000) != 0 &&
                (wparam == VK_UP || wparam == VK_DOWN))) {
            const auto move_up = wparam == VK_UP;
            const auto selected = service_.selection().snapshot().selected_ids;
            if (selected.size() == 1) {
                const auto row = std::find_if(view_model_.rows.begin(), view_model_.rows.end(),
                    [&selected](const TaskRowModel& value) { return value.id == selected.front(); });
                if (row != view_model_.rows.end()) {
                    const auto index = static_cast<std::size_t>(row - view_model_.rows.begin());
                    if (move_up && index > 0) {
                        static_cast<void>(service_.reorder(row->id,
                            view_model_.rows[index - 1].id, DropPosition::before));
                    } else if (!move_up && index + 1 < view_model_.rows.size()) {
                        static_cast<void>(service_.reorder(row->id,
                            view_model_.rows[index + 1].id, DropPosition::after));
                    }
                }
            }
            invalidate();
            return 0;
        }
        break;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window_, &point);
        const auto layout = current_layout(window_, dpi_);
        const auto logical_x = static_cast<float>(point.x) / (static_cast<float>(dpi_) / 96.0F);
        const auto logical_y = static_cast<float>(point.y) / (static_cast<float>(dpi_) / 96.0F);
        if (logical_x >= layout.task_list.x && logical_x < layout.task_list.right() &&
            logical_y >= layout.task_list.y && logical_y < layout.task_list.bottom()) {
            const auto wheel = GET_WHEEL_DELTA_WPARAM(wparam);
            scroll_y_ = std::max(0.0F, scroll_y_ -
                static_cast<float>(wheel) / WHEEL_DELTA * 3.0F * 58.0F);
            invalidate();
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        invalidate();
        return 0;
    case WM_TIMER:
        if (wparam == kMaintenanceTimer) static_cast<void>(service_.maintenance());
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        draw();
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        if (close_disposition(service_.snapshot().settings.close_to_tray) ==
            CloseDisposition::hide_to_tray) {
            hide_to_tray();
        } else {
            DestroyWindow(window_);
        }
        return 0;
    case WM_DESTROY:
        cancel_pointer_gesture();
        KillTimer(window_, kMaintenanceTimer);
        text_editor_.reset();
        details_panel_.reset();
        static_cast<void>(service_.flush());
        renderer_.reset();
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void WidgetWindow::draw() {
    if (!renderer_) return;
    const auto& state = service_.snapshot();
    QuerySpec query{
        .view = current_view_,
        .search = search_text_,
        .week_starts_on = state.settings.week_starts_on,
        .local_day = local_day};
    std::vector<std::wstring> previous_ids;
    previous_ids.reserve(view_model_.rows.size());
    for (const auto& row : view_model_.rows) previous_ids.push_back(row.id);
    const auto anchor_index = static_cast<std::size_t>(scroll_y_ / 58.0F);
    const auto anchor_offset = std::fmod(scroll_y_, 58.0F);
    auto next_model = build_view_model(state, query, now());
    if (!previous_ids.empty() && !next_model.rows.empty()) {
        std::vector<std::wstring> next_ids;
        next_ids.reserve(next_model.rows.size());
        for (const auto& row : next_model.rows) next_ids.push_back(row.id);
        scroll_y_ = static_cast<float>(preserve_scroll_anchor(
            previous_ids, next_ids, anchor_index)) * 58.0F + anchor_offset;
    }
    view_model_ = std::move(next_model);
    const auto layout = current_layout(window_, dpi_);
    scroll_y_ = std::clamp(scroll_y_, 0.0F,
        std::max(0.0F, static_cast<float>(view_model_.rows.size()) * 58.0F -
            layout.task_list.height));
    const auto palette = resolve_theme(state.settings.theme, read_system_theme());
    update_selection_toolbar();
    renderer_->draw(layout, palette, view_model_, current_view_,
        service_.selection().snapshot().selected_ids, scroll_y_,
        details_panel_ && details_panel_->visible(),
        selection_toolbar_ ? selection_toolbar_->state() : SelectionToolbarState{},
        selection_band_);
    if (text_editor_) {
        text_editor_->layout(layout, dpi_);
        if (text_editor_->active_control() == NativeEditorControl::inline_title) {
            const auto id = text_editor_->session_target_id();
            const auto row = std::find_if(view_model_.rows.begin(), view_model_.rows.end(),
                [&id](const TaskRowModel& value) { return value.id == id; });
            if (row != view_model_.rows.end()) {
                const auto index = static_cast<std::size_t>(row - view_model_.rows.begin());
                const auto y = layout.task_list.y + static_cast<float>(index) * 58.0F - scroll_y_;
                text_editor_->place_inline_title({layout.task_list.x + 48,
                    y + 8, layout.task_list.width - 112, 42}, dpi_);
            }
        }
    }
    if (details_panel_ && details_panel_->visible()) {
        const auto& tasks = state.tasks;
        const auto& id = details_panel_->task_id();
        const auto task = std::find_if(tasks.begin(), tasks.end(),
            [&id](const Task& value) { return value.id == id; });
        if (task == tasks.end()) {
            details_panel_->close();
            details_task_id_.clear();
        } else {
            details_panel_->layout(layout.task_list, dpi_);
        }
    }
}

void WidgetWindow::handle_pointer_down(POINT client_point) {
    if ((details_panel_ && details_panel_->visible()) || !pointer_controller_) return;
    const auto scale = static_cast<float>(dpi_) / 96.0F;
    const PointF logical{static_cast<float>(client_point.x) / scale,
        static_cast<float>(client_point.y) / scale};
    const auto layout = current_layout(window_, dpi_);
    if (selection_toolbar_ && selection_toolbar_->state().visible &&
        logical.y >= layout.footer.y && logical.y < layout.footer.bottom()) return;
    if (logical.x < layout.task_list.x || logical.x >= layout.task_list.right() ||
        logical.y < layout.task_list.y || logical.y >= layout.task_list.bottom()) return;
    const auto local_y = logical.y - layout.task_list.y + scroll_y_;
    RowHitArea area = RowHitArea::none;
    std::wstring id;
    if (local_y >= 0) {
        const auto index = static_cast<std::size_t>(local_y / 58.0F);
        if (index < view_model_.rows.size()) {
            const auto& row = view_model_.rows[index];
            id = row.id;
            const auto row_top = static_cast<float>(index) * 58.0F - scroll_y_;
            const RowHitZones zones{
                .row = {layout.task_list.x, layout.task_list.y + row_top,
                    layout.task_list.width, 58.0F},
                .checkbox = {layout.task_list.x + 10, layout.task_list.y + row_top + 17, 22, 22},
                .title = {layout.task_list.x + 50, layout.task_list.y + row_top + 8,
                    layout.task_list.width - 106, 42},
                .delete_button = {layout.task_list.right() - 48,
                    layout.task_list.y + row_top + 8, 24, 32},
                .drag_handle = {layout.task_list.right() - 24,
                    layout.task_list.y + row_top + 8, 16, 32}};
            area = hit_test_row(logical, zones);
        }
    }
    const auto action = pointer_controller_->press(area, id, logical,
        visible_task_ids(), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    handle_pointer_action(action);
    if (pointer_controller_->capturing()) SetCapture(window_);
    invalidate();
}

void WidgetWindow::handle_pointer_move(POINT client_point) {
    if (!pointer_controller_ || !pointer_controller_->active()) return;
    const auto scale = static_cast<float>(dpi_) / 96.0F;
    const PointF logical{static_cast<float>(client_point.x) / scale,
        static_cast<float>(client_point.y) / scale};
    const auto layout = current_layout(window_, dpi_);
    const PointF pointer_point{
        std::clamp(logical.x, layout.task_list.x, layout.task_list.right()),
        std::clamp(logical.y, layout.task_list.y, layout.task_list.bottom())};
    static_cast<void>(pointer_controller_->move(pointer_point, visible_task_bounds()));
    if (pointer_controller_->capturing() && GetCapture() != window_) SetCapture(window_);
    selection_band_ = pointer_controller_->rubber_band();
    update_selection_toolbar();
    invalidate();
}

void WidgetWindow::handle_pointer_up(POINT client_point) {
    if ((details_panel_ && details_panel_->visible())) return;
    const auto scale = static_cast<float>(dpi_) / 96.0F;
    const PointF logical{static_cast<float>(client_point.x) / scale,
        static_cast<float>(client_point.y) / scale};
    const auto layout = current_layout(window_, dpi_);
    const PointF pointer_point{
        std::clamp(logical.x, layout.task_list.x, layout.task_list.right()),
        std::clamp(logical.y, layout.task_list.y, layout.task_list.bottom())};
    update_selection_toolbar();
    if (selection_toolbar_ && selection_toolbar_->state().visible) {
        const auto action = selection_toolbar_action_at(logical.x, logical.y, layout.footer);
        if (action) {
            handle_toolbar_action(*action);
            invalidate();
            return;
        }
    }
    if (pointer_controller_ && pointer_controller_->active()) {
        const auto action = pointer_controller_->release(pointer_point, visible_task_bounds());
        handle_pointer_action(action);
        selection_band_.reset();
        if (GetCapture() == window_) ReleaseCapture();
        update_selection_toolbar();
        invalidate();
        return;
    }
    handle_pointer(client_point);
}

void WidgetWindow::handle_pointer(POINT client_point) {
    if (details_panel_ && details_panel_->visible()) return;
    const auto scale = static_cast<float>(dpi_) / 96.0F;
    const PointF logical{static_cast<float>(client_point.x) / scale,
        static_cast<float>(client_point.y) / scale};
    const auto layout = current_layout(window_, dpi_);
    if (selection_toolbar_ && selection_toolbar_->state().visible &&
        logical.y >= layout.footer.y && logical.y < layout.footer.bottom()) return;
    if (logical.x >= layout.tabs.x && logical.x < layout.tabs.right() &&
        logical.y >= layout.tabs.y && logical.y < layout.tabs.bottom()) {
        const auto tab_width = layout.tabs.width / 4.0F;
        const auto index = static_cast<std::size_t>(
            (logical.x - layout.tabs.x) / tab_width);
        constexpr ViewKind views[]{ViewKind::today, ViewKind::week, ViewKind::all, ViewKind::done};
        if (index < std::size(views) && current_view_ != views[index]) {
            current_view_ = views[index];
            scroll_y_ = 0;
            view_model_ = {};
            invalidate();
        }
        return;
    }
}

std::vector<std::wstring> WidgetWindow::visible_task_ids() const {
    std::vector<std::wstring> ids;
    ids.reserve(view_model_.rows.size());
    for (const auto& row : view_model_.rows) ids.push_back(row.id);
    return ids;
}

std::vector<PointerRowBounds> WidgetWindow::visible_task_bounds() const {
    std::vector<PointerRowBounds> rows;
    const auto layout = current_layout(window_, dpi_);
    rows.reserve(view_model_.rows.size());
    for (std::size_t index = 0; index < view_model_.rows.size(); ++index) {
        const auto y = layout.task_list.y + static_cast<float>(index) * 58.0F - scroll_y_;
        if (y + 58.0F <= layout.task_list.y || y >= layout.task_list.bottom()) continue;
        rows.push_back({view_model_.rows[index].id,
            {layout.task_list.x, y, layout.task_list.width, 58.0F}});
    }
    return rows;
}

void WidgetWindow::handle_pointer_action(const PointerAction& action) {
    switch (action.kind) {
    case PointerActionKind::toggle_completion: {
        const auto row = std::find_if(view_model_.rows.begin(), view_model_.rows.end(),
            [&action](const TaskRowModel& value) { return value.id == action.task_id; });
        if (row != view_model_.rows.end())
            static_cast<void>(service_.set_completed(action.task_id, row->status != TaskStatus::done));
        break;
    }
    case PointerActionKind::delete_task:
        static_cast<void>(service_.delete_tasks({action.task_id}));
        break;
    case PointerActionKind::begin_inline_edit: {
        const auto row = std::find_if(view_model_.rows.begin(), view_model_.rows.end(),
            [&action](const TaskRowModel& value) { return value.id == action.task_id; });
        if (row == view_model_.rows.end() || !text_editor_) break;
        commit_inline_title();
        text_editor_->begin_inline_title(row->id, row->title);
        const auto index = static_cast<std::size_t>(row - view_model_.rows.begin());
        const auto layout = current_layout(window_, dpi_);
        const auto y = layout.task_list.y + static_cast<float>(index) * 58.0F - scroll_y_;
        text_editor_->place_inline_title({layout.task_list.x + 48,
            y + 8, layout.task_list.width - 112, 42}, dpi_);
        break;
    }
    case PointerActionKind::reorder:
        static_cast<void>(service_.reorder(action.task_id, action.target_id, action.position));
        break;
    case PointerActionKind::selection_changed:
    case PointerActionKind::begin_drag:
    case PointerActionKind::rubber_band:
    case PointerActionKind::end_rubber_band:
    case PointerActionKind::none:
        break;
    }
    update_selection_toolbar();
}

void WidgetWindow::handle_toolbar_action(SelectionToolbarAction action) {
    if (!selection_toolbar_ || selection_toolbar_->state().selected_count == 0) return;
    const auto selected = service_.selection().snapshot().selected_ids;
    switch (action) {
    case SelectionToolbarAction::complete: {
        const auto& tasks = service_.snapshot().tasks;
        const bool any_incomplete = std::any_of(selected.begin(), selected.end(),
            [&tasks](const std::wstring& id) {
                const auto task = std::find_if(tasks.begin(), tasks.end(),
                    [&id](const Task& value) { return value.id == id; });
                return task != tasks.end() && task->status != TaskStatus::done;
            });
        static_cast<void>(selection_toolbar_->set_completed(any_incomplete));
        break;
    }
    case SelectionToolbarAction::high_priority: {
        TaskPatch patch;
        patch.priority = Priority::high;
        static_cast<void>(selection_toolbar_->apply_patch(patch));
        break;
    }
    case SelectionToolbarAction::edit_details: {
        if (selected.empty() || !details_panel_) break;
        const auto& tasks = service_.snapshot().tasks;
        const auto task = std::find_if(tasks.begin(), tasks.end(),
            [&selected](const Task& value) { return value.id == selected.front(); });
        if (task == tasks.end()) break;
        details_task_id_ = task->id;
        details_batch_ = selected.size() > 1;
        details_panel_->open(*task, current_layout(window_, dpi_).task_list, dpi_);
        ensure_details_window_height();
        break;
    }
    case SelectionToolbarAction::delete_selected:
        static_cast<void>(selection_toolbar_->delete_selected());
        break;
    case SelectionToolbarAction::clear:
        selection_toolbar_->clear();
        break;
    }
}

void WidgetWindow::update_selection_toolbar() {
    if (!selection_toolbar_) return;
    selection_toolbar_->update(service_.selection().snapshot().selected_ids, visible_task_ids());
}

void WidgetWindow::cancel_pointer_gesture() {
    if (pointer_controller_) pointer_controller_->cancel();
    selection_band_.reset();
    if (window_ != nullptr && GetCapture() == window_) ReleaseCapture();
}

void WidgetWindow::commit_inline_title() {
    if (!text_editor_ || text_editor_->active_control() != NativeEditorControl::inline_title) return;
    const auto result = text_editor_->commit_active();
    if (!result) return;
    TaskPatch patch;
    patch.title = result->text;
    static_cast<void>(service_.update_task(result->target_id, patch));
    invalidate();
}

void WidgetWindow::clamp_to_monitor() {
    if (window_ == nullptr) return;
    RECT window_rect{};
    GetWindowRect(window_, &window_rect);
    const auto monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return;
    const auto clamped = desktop_todo::clamp_to_work_area(
        {window_rect.left, window_rect.top,
            window_rect.right - window_rect.left, window_rect.bottom - window_rect.top},
        {{info.rcWork.left, info.rcWork.top,
            info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top},
            static_cast<float>(dpi_)});
    SetWindowPos(window_, nullptr, clamped.x, clamped.y, clamped.width, clamped.height,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void WidgetWindow::ensure_details_window_height() {
    if (window_ == nullptr) return;
    RECT bounds{};
    if (!GetWindowRect(window_, &bounds)) return;
    const auto required_height = MulDiv(
        static_cast<int>(kDetailsWindowMinimumHeight), static_cast<int>(dpi_), 96);
    if (bounds.bottom - bounds.top < required_height) {
        SetWindowPos(window_, nullptr, bounds.left, bounds.top,
            bounds.right - bounds.left, required_height,
            SWP_NOACTIVATE | SWP_NOZORDER);
        clamp_to_monitor();
    }
}

LRESULT WidgetWindow::hit_test(POINT screen) const {
    RECT window{};
    GetWindowRect(window_, &window);
    const auto border = std::max(4, MulDiv(6, static_cast<int>(dpi_), 96));
    const bool left = screen.x < window.left + border;
    const bool right = screen.x >= window.right - border;
    const bool top = screen.y < window.top + border;
    const bool bottom = screen.y >= window.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    if (screen.y < window.top + MulDiv(56, static_cast<int>(dpi_), 96)) return HTCAPTION;
    return HTCLIENT;
}

}  // namespace desktop_todo
