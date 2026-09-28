#include "presentation/text_editor.h"

#define NOMINMAX
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace desktop_todo {
namespace {

constexpr int kQuickAddId = 501;
constexpr int kInlineTitleId = 502;
constexpr int kSearchId = 503;
constexpr wchar_t kFocusGenerationProperty[] = L"DesktopTodoList.EditorFocusGeneration";
int px(float logical, UINT dpi) {
    return static_cast<int>(std::lround(logical * static_cast<float>(dpi) / 96.0F));
}

}  // namespace

bool TextEditor::create(HWND parent, UINT dpi) {
    parent_ = parent;
    dpi_ = dpi;
    quick_add_ = create_edit(kQuickAddId, ES_AUTOHSCROLL, L"添加任务…");
    inline_title_ = create_edit(kInlineTitleId, ES_AUTOHSCROLL, nullptr);
    search_ = create_edit(kSearchId, ES_AUTOHSCROLL, L"搜索标题和备注…");
    if (quick_add_ == nullptr || inline_title_ == nullptr || search_ == nullptr) {
        destroy();
        return false;
    }
    SendMessageW(quick_add_, EM_SETLIMITTEXT, 200, 0);
    SendMessageW(inline_title_, EM_SETLIMITTEXT, 200, 0);
    SendMessageW(search_, EM_SETLIMITTEXT, 200, 0);
    ShowWindow(search_, SW_SHOWNA);
    return true;
}

void TextEditor::destroy() noexcept {
    for (const auto child : {quick_add_, inline_title_, search_}) {
        if (child != nullptr) DestroyWindow(child);
    }
    quick_add_ = inline_title_ = search_ = nullptr;
    parent_ = nullptr;
    active_ = NativeEditorControl::none;
    session_.cancel();
}

void TextEditor::layout(const LayoutResult& result, UINT dpi) {
    dpi_ = dpi;
    const auto place = [this, dpi](HWND child, const RectF& bounds) {
        SetWindowPos(child, nullptr, px(bounds.x, dpi), px(bounds.y, dpi),
            std::max(1, px(bounds.width, dpi)), std::max(1, px(bounds.height, dpi)),
            SWP_NOACTIVATE | SWP_NOZORDER);
    };
    place(quick_add_, {result.quick_add.x + 12, result.quick_add.y + 5,
        result.quick_add.width - 24, result.quick_add.height - 10});
    place(search_, {result.logical_client.width - 210, 15, 190, 28});
}

void TextEditor::place_inline_title(const RectF& bounds, UINT dpi) {
    SetWindowPos(inline_title_, nullptr, px(bounds.x, dpi), px(bounds.y, dpi),
        std::max(1, px(bounds.width, dpi)), std::max(1, px(bounds.height, dpi)),
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void TextEditor::begin_new_task() {
    active_ = NativeEditorControl::quick_add;
    session_.begin_new_task();
    SetWindowTextW(quick_add_, L"");
    show_active();
}

void TextEditor::begin_inline_title(std::wstring id, std::wstring title) {
    auto generation = reinterpret_cast<ULONG_PTR>(
        GetPropW(inline_title_, kFocusGenerationProperty)) + 1U;
    if (generation == 0) generation = 1;
    SetPropW(inline_title_, kFocusGenerationProperty,
        reinterpret_cast<HANDLE>(generation));
    active_ = NativeEditorControl::inline_title;
    session_.begin_inline_title(std::move(id), title);
    SetWindowTextW(inline_title_, title.c_str());
    show_active();
}

void TextEditor::focus_search() {
    active_ = NativeEditorControl::search;
    session_.begin_search(search_text_);
    show_active();
}

void TextEditor::close_inline() noexcept {
    if (active_ != NativeEditorControl::inline_title) return;
    ShowWindow(inline_title_, SW_HIDE);
    session_.cancel();
    active_ = NativeEditorControl::none;
}

void TextEditor::cancel_active() noexcept {
    if (active_ == NativeEditorControl::inline_title) {
        close_inline();
        return;
    }
    if (active_ == NativeEditorControl::quick_add) {
        SetWindowTextW(quick_add_, L"");
        ShowWindow(quick_add_, SW_HIDE);
    } else if (active_ == NativeEditorControl::search) {
        search_text_.clear();
        SetWindowTextW(search_, L"");
    }
    session_.cancel();
    active_ = NativeEditorControl::none;
    SetFocus(parent_);
}

void TextEditor::clear_search() noexcept {
    search_text_.clear();
    if (search_ != nullptr) SetWindowTextW(search_, L"");
    if (active_ == NativeEditorControl::search) {
        session_.begin_search({});
    }
}

std::optional<EditorCommit> TextEditor::commit_active() {
    if (active_ == NativeEditorControl::none) return std::nullopt;
    const auto child = active_handle();
    capture_text(child);
    auto result = session_.commit();
    if (result) {
        if (active_ == NativeEditorControl::quick_add) {
            SetWindowTextW(quick_add_, L"");
        } else if (active_ == NativeEditorControl::inline_title) {
            ShowWindow(inline_title_, SW_HIDE);
            active_ = NativeEditorControl::none;
        }
    }
    return result;
}

NativeEditorControl TextEditor::control_for(HWND child) const noexcept {
    if (child == quick_add_) return NativeEditorControl::quick_add;
    if (child == inline_title_) return NativeEditorControl::inline_title;
    if (child == search_) return NativeEditorControl::search;
    return NativeEditorControl::none;
}

NativeEditorControl TextEditor::active_control() const noexcept { return active_; }

HWND TextEditor::active_handle() const noexcept {
    switch (active_) {
    case NativeEditorControl::quick_add: return quick_add_;
    case NativeEditorControl::inline_title: return inline_title_;
    case NativeEditorControl::search: return search_;
    default: return nullptr;
    }
}

HWND TextEditor::search_handle() const noexcept { return search_; }
const std::wstring& TextEditor::search_text() const noexcept { return search_text_; }
const std::wstring& TextEditor::session_target_id() const noexcept {
    return session_.target_id();
}
std::uint32_t TextEditor::focus_generation() const noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<ULONG_PTR>(
        GetPropW(inline_title_, kFocusGenerationProperty)));
}

void TextEditor::capture_text(HWND child) {
    if (child == nullptr) return;
    const auto length = GetWindowTextLengthW(child);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(child, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    if (child == search_) {
        search_text_ = text;
        if (active_ == NativeEditorControl::search) session_.set_text(text);
    } else if (child == active_handle()) {
        session_.set_text(std::move(text));
    }
}

LRESULT CALLBACK TextEditor::edit_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference_data) {
    const auto parent = reinterpret_cast<HWND>(reference_data);
    if (message == WM_KEYDOWN) {
        if (wparam == VK_RETURN) {
            PostMessageW(parent, kEditorCommitMessage, 0, 0);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(parent, kEditorCancelMessage, 0, 0);
            return 0;
        }
        if (wparam == 'N' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            PostMessageW(parent, kEditorNewTaskMessage, 0, 0);
            return 0;
        }
        if (wparam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            PostMessageW(parent, kEditorSearchMessage, 0, 0);
            return 0;
        }
        if ((wparam == VK_UP || wparam == VK_DOWN) &&
            (GetKeyState(VK_MENU) & 0x8000) != 0) {
            PostMessageW(parent, kEditorMoveTaskMessage, wparam, 0);
            return 0;
        }
    } else if (message == WM_KILLFOCUS) {
        const auto generation = reinterpret_cast<ULONG_PTR>(
            GetPropW(window, kFocusGenerationProperty));
        PostMessageW(parent, kEditorFocusLostMessage,
            static_cast<WPARAM>(generation), reinterpret_cast<LPARAM>(window));
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

HWND TextEditor::create_edit(int control_id, DWORD style, const wchar_t* cue) {
    const auto control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_LEFT | style,
        0, 0, 1, 1, parent_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent_, GWLP_HINSTANCE)), nullptr);
    if (control == nullptr) return nullptr;
    SetWindowSubclass(control, &TextEditor::edit_proc,
        reinterpret_cast<UINT_PTR>(control), reinterpret_cast<DWORD_PTR>(parent_));
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    if (cue != nullptr) SendMessageW(control, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(cue));
    ShowWindow(control, SW_HIDE);
    return control;
}

void TextEditor::show_active() noexcept {
    ShowWindow(quick_add_, active_ == NativeEditorControl::quick_add ? SW_SHOWNA : SW_HIDE);
    ShowWindow(inline_title_, active_ == NativeEditorControl::inline_title ? SW_SHOWNA : SW_HIDE);
    ShowWindow(search_, SW_SHOWNA);
    const auto handle = active_handle();
    if (handle != nullptr) {
        SetFocus(handle);
        SendMessageW(handle, EM_SETSEL,
            active_ == NativeEditorControl::inline_title ? 0 : -1,
            -1);
    }
}

}  // namespace desktop_todo
