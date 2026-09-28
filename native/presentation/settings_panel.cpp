#include "presentation/settings_panel.h"

#include "domain/validation.h"

#include <commctrl.h>
#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace desktop_todo {
namespace {

constexpr wchar_t kSettingsClass[] = L"DesktopTodoList.SettingsPanel.v2";
constexpr int kControlIdBase = 1000;
constexpr int kTheme = 0;
constexpr int kView = 1;
constexpr int kWeek = 2;
constexpr int kCloseToTray = 3;
constexpr int kShowHotkey = 4;
constexpr int kRecoveryHotkey = 5;
constexpr int kLayer = 6;
constexpr int kSelectable = 7;
constexpr int kTimeout = 8;
constexpr int kMultiSelect = 9;
constexpr int kRubberBand = 10;
constexpr int kSave = 11;
constexpr int kCancel = 12;
constexpr int kImport = 13;
constexpr int kExport = 14;
constexpr int kReset = 15;

void add_combo_item(HWND control, const wchar_t* text, int selected) {
    const auto index = static_cast<int>(SendMessageW(control, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(text)));
    if (index == selected) SendMessageW(control, CB_SETCURSEL, index, 0);
}

std::wstring control_text(HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(length));
    return result;
}

}  // namespace

std::vector<std::wstring> validate_settings_draft(const Settings& settings) {
    std::vector<std::wstring> errors;
    const auto show_hotkey = parse_hotkey(settings.hotkey);
    const auto recovery_hotkey = parse_hotkey(settings.selectable_hotkey);
    if (!show_hotkey) errors.push_back(L"全局显示快捷键格式无效。");
    if (!recovery_hotkey) errors.push_back(L"交互恢复快捷键格式无效。");
    if (show_hotkey && recovery_hotkey && *show_hotkey == *recovery_hotkey)
        errors.push_back(L"显示快捷键与交互恢复快捷键不能相同。");
    if (settings.click_through_timeout_minutes != 0 &&
        settings.click_through_timeout_minutes != 30)
        errors.push_back(L"鼠标穿透自动恢复时间只能关闭或设为 30 分钟。");

    AppState candidate;
    candidate.settings = settings;
    const auto validation = validate_state(std::move(candidate), Clock::time_point{});
    if (!validation.issues.empty()) errors.push_back(L"设置中包含超出允许范围的值。");
    return errors;
}

SettingsApplyResult commit_settings_draft(
    const Settings& current, const Settings& draft, const SettingsApplyApi& api) {
    const auto errors = validate_settings_draft(draft);
    if (!errors.empty()) return {false, true, errors.front()};
    if (!draft.selectable && (!api.escape_routes_available || !api.escape_routes_available()))
        return {false, true, L"鼠标穿透需要通知区域和交互恢复快捷键同时可用。"};

    bool show_hotkey_changed = false;
    bool recovery_hotkey_changed = false;
    bool layer_changed = false;
    bool click_through_changed = false;
    bool rollback_complete = true;
    const auto restore_hotkeys = [&] {
        if (recovery_hotkey_changed) {
            const auto restored = api.replace_hotkey(
                HotkeyAction::toggle_interaction, current.selectable_hotkey);
            rollback_complete = rollback_complete && restored.success;
        }
        if (show_hotkey_changed) {
            const auto restored = api.replace_hotkey(HotkeyAction::show_hide, current.hotkey);
            rollback_complete = rollback_complete && restored.success;
        }
    };
    const auto failed = [&rollback_complete](std::wstring error) {
        return SettingsApplyResult{false, rollback_complete, std::move(error)};
    };

    if (draft.hotkey != current.hotkey) {
        const auto registered = api.replace_hotkey(HotkeyAction::show_hide, draft.hotkey);
        if (!registered.success) return {false, true, L"全局显示快捷键注册失败或发生冲突。"};
        show_hotkey_changed = true;
    }
    if (draft.selectable_hotkey != current.selectable_hotkey) {
        const auto registered = api.replace_hotkey(
            HotkeyAction::toggle_interaction, draft.selectable_hotkey);
        if (!registered.success) {
            restore_hotkeys();
            return failed(L"交互恢复快捷键注册失败；原设置已恢复。 ");
        }
        recovery_hotkey_changed = true;
    }
    if (draft.window_layer != current.window_layer) {
        if (!api.set_layer || !api.set_layer(draft.window_layer)) {
            restore_hotkeys();
            return failed(L"窗口层级切换失败；原设置已恢复。");
        }
        layer_changed = true;
    }
    if (draft.selectable != current.selectable) {
        if (!api.set_click_through || !api.set_click_through(!draft.selectable)) {
            if (layer_changed) rollback_complete = api.set_layer(current.window_layer) && rollback_complete;
            restore_hotkeys();
            return failed(L"鼠标交互模式切换失败；原设置已恢复。");
        }
        click_through_changed = true;
    }
    if (!api.save || !api.save(draft)) {
        if (click_through_changed)
            rollback_complete = api.set_click_through(!current.selectable) && rollback_complete;
        if (layer_changed)
            rollback_complete = api.set_layer(current.window_layer) && rollback_complete;
        restore_hotkeys();
        return failed(L"设置保存失败；已尝试恢复所有平台状态。");
    }
    return {true, true, {}};
}

bool SettingsPanel::show_modal(
    HWND owner, const Settings& initial, Commit commit, Transfer transfer) {
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.lpfnWndProc = &SettingsPanel::window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kSettingsClass;
    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    owner_ = owner;
    initial_ = initial;
    commit_ = std::move(commit);
    transfer_ = std::move(transfer);
    closed_ = false;
    committed_ = false;
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME,
        kSettingsClass, L"DesktopTodoList 设置",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 610, owner_, nullptr,
        window_class.hInstance, this);
    if (window_ == nullptr) return false;
    create_controls();
    RECT bounds{};
    if (owner_ != nullptr && GetWindowRect(owner_, &bounds)) {
        const auto x = bounds.left + ((bounds.right - bounds.left) - 500) / 2;
        const auto y = bounds.top + ((bounds.bottom - bounds.top) - 610) / 2;
        SetWindowPos(window_, HWND_TOP, x, y, 500, 610, SWP_SHOWWINDOW);
    } else {
        ShowWindow(window_, SW_SHOWNORMAL);
    }
    if (owner_ != nullptr) EnableWindow(owner_, FALSE);
    SetForegroundWindow(window_);

    MSG message{};
    while (!closed_) {
        const auto status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            close(false);
            if (status == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner_ != nullptr) {
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
    }
    return committed_;
}

LRESULT CALLBACK SettingsPanel::window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    SettingsPanel* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<SettingsPanel*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self ? self->handle_message(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

void SettingsPanel::create_controls() {
    const auto make_label = [this](int y, const wchar_t* text) {
        CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            18, y + 4, 165, 24, window_, nullptr, nullptr, nullptr);
    };
    const auto make_combo = [this, &make_label](int index, int y, const wchar_t* label) {
        make_label(y, label);
        controls_[index] = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            190, y, 270, 240, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlIdBase + index)),
            GetModuleHandleW(nullptr), nullptr);
    };
    const auto make_check = [this, &make_label](int index, int y, const wchar_t* label) {
        controls_[index] = CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            190, y, 270, 26, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlIdBase + index)),
            GetModuleHandleW(nullptr), nullptr);
    };
    const auto make_edit = [this, &make_label](int index, int y, const wchar_t* label) {
        make_label(y, label);
        controls_[index] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            190, y, 270, 26, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlIdBase + index)),
            GetModuleHandleW(nullptr), nullptr);
    };

    make_combo(kTheme, 16, L"主题");
    add_combo_item(controls_[kTheme], L"跟随系统", 0);
    add_combo_item(controls_[kTheme], L"浅色", 1);
    add_combo_item(controls_[kTheme], L"深色", 2);
    SendMessageW(controls_[kTheme], CB_SETCURSEL,
        initial_.theme == Theme::light ? 1 : initial_.theme == Theme::dark ? 2 : 0, 0);

    make_combo(kView, 56, L"默认视图");
    add_combo_item(controls_[kView], L"今天", 0);
    add_combo_item(controls_[kView], L"本周", 1);
    add_combo_item(controls_[kView], L"全部", 2);
    add_combo_item(controls_[kView], L"已完成", 3);
    SendMessageW(controls_[kView], CB_SETCURSEL,
        initial_.default_filter == ViewKind::week ? 1 : initial_.default_filter == ViewKind::all ? 2 :
        initial_.default_filter == ViewKind::done ? 3 : 0, 0);

    make_combo(kWeek, 96, L"每周起始日");
    add_combo_item(controls_[kWeek], L"星期一", 0);
    add_combo_item(controls_[kWeek], L"星期日", 1);
    SendMessageW(controls_[kWeek], CB_SETCURSEL, initial_.week_starts_on == 0 ? 0 : 1, 0);
    make_check(kCloseToTray, 136, L"关闭窗口时收起到通知区域");
    SendMessageW(controls_[kCloseToTray], BM_SETCHECK,
        initial_.close_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);
    make_edit(kShowHotkey, 176, L"显示/隐藏快捷键");
    SetWindowTextW(controls_[kShowHotkey], initial_.hotkey.c_str());
    make_edit(kRecoveryHotkey, 216, L"交互恢复快捷键");
    SetWindowTextW(controls_[kRecoveryHotkey], initial_.selectable_hotkey.c_str());

    make_combo(kLayer, 256, L"窗口层级");
    add_combo_item(controls_[kLayer], L"普通", 0);
    add_combo_item(controls_[kLayer], L"置顶", 1);
    add_combo_item(controls_[kLayer], L"置底", 2);
    SendMessageW(controls_[kLayer], CB_SETCURSEL,
        initial_.window_layer == WindowLayer::top ? 1 :
        initial_.window_layer == WindowLayer::bottom ? 2 : 0, 0);

    make_check(kSelectable, 296, L"允许鼠标交互（关闭即穿透）");
    SendMessageW(controls_[kSelectable], BM_SETCHECK,
        initial_.selectable ? BST_CHECKED : BST_UNCHECKED, 0);
    make_combo(kTimeout, 336, L"穿透自动恢复");
    add_combo_item(controls_[kTimeout], L"关闭", 0);
    add_combo_item(controls_[kTimeout], L"30 分钟", 1);
    SendMessageW(controls_[kTimeout], CB_SETCURSEL,
        initial_.click_through_timeout_minutes == 30 ? 1 : 0, 0);
    make_check(kMultiSelect, 376, L"启用多选");
    SendMessageW(controls_[kMultiSelect], BM_SETCHECK,
        initial_.multi_select_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    make_check(kRubberBand, 406, L"启用框选");
    SendMessageW(controls_[kRubberBand], BM_SETCHECK,
        initial_.rubber_band_select ? BST_CHECKED : BST_UNCHECKED, 0);

    controls_[kImport] = CreateWindowExW(0, L"BUTTON", L"导入 JSON…",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 18, 452, 132, 30, window_,
        reinterpret_cast<HMENU>(kControlIdBase + kImport), GetModuleHandleW(nullptr), nullptr);
    controls_[kExport] = CreateWindowExW(0, L"BUTTON", L"导出 JSON…",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 158, 452, 132, 30, window_,
        reinterpret_cast<HMENU>(kControlIdBase + kExport), GetModuleHandleW(nullptr), nullptr);
    controls_[kReset] = CreateWindowExW(0, L"BUTTON", L"重置数据…",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 298, 452, 162, 30, window_,
        reinterpret_cast<HMENU>(kControlIdBase + kReset), GetModuleHandleW(nullptr), nullptr);
    controls_[kSave] = CreateWindowExW(0, L"BUTTON", L"保存设置",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 264, 510, 96, 32,
        window_, reinterpret_cast<HMENU>(kControlIdBase + kSave), GetModuleHandleW(nullptr), nullptr);
    controls_[kCancel] = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 364, 510, 96, 32, window_,
        reinterpret_cast<HMENU>(kControlIdBase + kCancel), GetModuleHandleW(nullptr), nullptr);
    update_controls(initial_);
}

void SettingsPanel::update_controls(const Settings& settings) {
    const auto set_combo = [this](int index, int selected) {
        SendMessageW(controls_[index], CB_SETCURSEL, selected, 0);
    };
    set_combo(kTheme, settings.theme == Theme::light ? 1 : settings.theme == Theme::dark ? 2 : 0);
    set_combo(kView, settings.default_filter == ViewKind::week ? 1 :
        settings.default_filter == ViewKind::all ? 2 : settings.default_filter == ViewKind::done ? 3 : 0);
    set_combo(kWeek, settings.week_starts_on == 0 ? 0 : 1);
    SendMessageW(controls_[kCloseToTray], BM_SETCHECK,
        settings.close_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(controls_[kShowHotkey], settings.hotkey.c_str());
    SetWindowTextW(controls_[kRecoveryHotkey], settings.selectable_hotkey.c_str());
    set_combo(kLayer, settings.window_layer == WindowLayer::top ? 1 :
        settings.window_layer == WindowLayer::bottom ? 2 : 0);
    SendMessageW(controls_[kSelectable], BM_SETCHECK,
        settings.selectable ? BST_CHECKED : BST_UNCHECKED, 0);
    set_combo(kTimeout, settings.click_through_timeout_minutes == 30 ? 1 : 0);
    SendMessageW(controls_[kMultiSelect], BM_SETCHECK,
        settings.multi_select_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(controls_[kRubberBand], BM_SETCHECK,
        settings.rubber_band_select ? BST_CHECKED : BST_UNCHECKED, 0);
}

Settings SettingsPanel::read_draft() const {
    auto draft = initial_;
    const auto selected = [this](int index) {
        return static_cast<int>(SendMessageW(controls_[index], CB_GETCURSEL, 0, 0));
    };
    switch (selected(kTheme)) {
    case 1: draft.theme = Theme::light; break;
    case 2: draft.theme = Theme::dark; break;
    default: draft.theme = Theme::system; break;
    }
    switch (selected(kView)) {
    case 1: draft.default_filter = ViewKind::week; break;
    case 2: draft.default_filter = ViewKind::all; break;
    case 3: draft.default_filter = ViewKind::done; break;
    default: draft.default_filter = ViewKind::today; break;
    }
    draft.week_starts_on = selected(kWeek) == 0 ? 0 : 1;
    draft.close_to_tray = SendMessageW(controls_[kCloseToTray], BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft.hotkey = control_text(controls_[kShowHotkey]);
    draft.selectable_hotkey = control_text(controls_[kRecoveryHotkey]);
    draft.window_layer = selected(kLayer) == 1 ? WindowLayer::top :
        selected(kLayer) == 2 ? WindowLayer::bottom : WindowLayer::normal;
    draft.selectable = SendMessageW(controls_[kSelectable], BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft.click_through_timeout_minutes = selected(kTimeout) == 1 ? 30 : 0;
    draft.multi_select_enabled = SendMessageW(controls_[kMultiSelect], BM_GETCHECK, 0, 0) == BST_CHECKED;
    draft.rubber_band_select = SendMessageW(controls_[kRubberBand], BM_GETCHECK, 0, 0) == BST_CHECKED;
    return draft;
}

LRESULT SettingsPanel::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) {
        close(false);
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
        switch (LOWORD(wparam) - kControlIdBase) {
        case kSave: {
            const auto result = commit_ ? commit_(initial_, read_draft())
                                        : SettingsApplyResult{false, true, L"无法保存设置。"};
            if (result.success) close(true);
            else MessageBoxW(window_, result.error.c_str(), L"DesktopTodoList 设置",
                MB_OK | MB_ICONWARNING);
            return 0;
        }
        case kCancel:
            close(false);
            return 0;
        case kImport:
            if (transfer_) {
                if (auto updated = transfer_(window_, SettingsTransferAction::import_json)) {
                    initial_ = std::move(*updated);
                    update_controls(initial_);
                }
            }
            return 0;
        case kExport:
            if (transfer_) {
                if (auto updated = transfer_(window_, SettingsTransferAction::export_json)) {
                    initial_ = std::move(*updated);
                    update_controls(initial_);
                }
            }
            return 0;
        case kReset:
            if (transfer_) {
                if (auto updated = transfer_(window_, SettingsTransferAction::reset_data)) {
                    initial_ = std::move(*updated);
                    update_controls(initial_);
                }
            }
            return 0;
        default:
            break;
        }
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void SettingsPanel::close(bool committed) {
    committed_ = committed;
    closed_ = true;
    const auto window = window_;
    window_ = nullptr;
    if (window != nullptr) DestroyWindow(window);
}

}  // namespace desktop_todo
