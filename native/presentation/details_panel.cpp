#include "presentation/details_panel.h"
#include "presentation/text_editor.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace desktop_todo {
namespace {

constexpr std::size_t kMaximumNoteLength = 2'000;
constexpr std::size_t kMaximumTagLength = 24;

std::wstring trim(std::wstring value) {
    const auto not_space = [](wchar_t character) {
        return std::iswspace(static_cast<wint_t>(character)) == 0;
    };
    const auto first = std::find_if(value.begin(), value.end(), not_space);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return {first, last};
}

std::vector<std::wstring> parse_tags(std::wstring_view text) {
    std::vector<std::wstring> tags;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find_first_of(L",;，；", start);
        auto tag = trim(std::wstring{text.substr(start,
            end == std::wstring_view::npos ? text.size() - start : end - start)});
        if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(std::move(tag));
        }
        if (end == std::wstring_view::npos) break;
        start = end + 1;
    }
    return tags;
}

std::wstring read_text(HWND control) {
    const auto length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

int scale_px(float logical, UINT dpi) {
    return static_cast<int>(std::lround(logical * static_cast<float>(dpi) / 96.0F));
}

std::optional<std::optional<Clock::time_point>> parse_due_text(std::wstring_view text) {
    const auto normalized = trim(std::wstring{text});
    if (normalized.empty()) return std::optional<Clock::time_point>{};
    if (normalized.size() != 16 || normalized[4] != L'-' || normalized[7] != L'-' ||
        normalized[10] != L' ' || normalized[13] != L':') return std::nullopt;
    const auto number = [&normalized](std::size_t offset, std::size_t length) -> std::optional<int> {
        int value = 0;
        for (std::size_t index = 0; index < length; ++index) {
            const auto character = normalized[offset + index];
            if (character < L'0' || character > L'9') return std::nullopt;
            value = value * 10 + static_cast<int>(character - L'0');
        }
        return value;
    };
    const auto year = number(0, 4);
    const auto month = number(5, 2);
    const auto day = number(8, 2);
    const auto hour = number(11, 2);
    const auto minute = number(14, 2);
    if (!year || !month || !day || !hour || !minute ||
        *hour > 23 || *minute > 59 ||
        !std::chrono::year_month_day{std::chrono::year{*year},
            std::chrono::month{static_cast<unsigned>(*month)},
            std::chrono::day{static_cast<unsigned>(*day)}}.ok()) return std::nullopt;

    SYSTEMTIME local{};
    local.wYear = static_cast<WORD>(*year);
    local.wMonth = static_cast<WORD>(*month);
    local.wDay = static_cast<WORD>(*day);
    local.wHour = static_cast<WORD>(*hour);
    local.wMinute = static_cast<WORD>(*minute);
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTimeEx(nullptr, &local, &utc)) return std::nullopt;
    FILETIME file_time{};
    if (!SystemTimeToFileTime(&utc, &file_time)) return std::nullopt;
    ULARGE_INTEGER ticks{};
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    constexpr std::uint64_t epoch_offset = 116'444'736'000'000'000ULL;
    if (ticks.QuadPart < epoch_offset) return std::nullopt;
    const auto milliseconds = static_cast<std::int64_t>((ticks.QuadPart - epoch_offset) / 10'000ULL);
    return Clock::time_point{std::chrono::milliseconds{milliseconds}};
}

}  // namespace

DetailsDraft::DetailsDraft(const Task& task)
    : original_(task), note_(task.note), priority_(task.priority),
      due_text_(format_due_text(task.due_at)), remind_(task.remind) {
    for (std::size_t index = 0; index < task.tags.size(); ++index) {
        if (index != 0) tags_text_ += L", ";
        tags_text_ += task.tags[index];
    }
}

void DetailsDraft::set_note(std::wstring note) { note_ = std::move(note); }
void DetailsDraft::set_priority(Priority priority) noexcept { priority_ = priority; }
void DetailsDraft::set_due_text(std::wstring value) { due_text_ = std::move(value); }
void DetailsDraft::set_remind(bool remind) noexcept { remind_ = remind; }
void DetailsDraft::set_tags_text(std::wstring value) { tags_text_ = std::move(value); }

std::optional<TaskPatch> DetailsDraft::make_patch() const {
    const auto due = parse_due_text(due_text_);
    if (!due.has_value() || note_.size() > kMaximumNoteLength) return std::nullopt;
    const auto tags = parse_tags(tags_text_);
    if (tags.size() > 5 || std::any_of(tags.begin(), tags.end(),
        [](const std::wstring& tag) { return tag.size() > kMaximumTagLength; })) return std::nullopt;

    TaskPatch patch;
    if (note_ != original_.note) patch.note = note_;
    if (priority_ != original_.priority) patch.priority = priority_;
    if (*due != original_.due_at) patch.due_at = *due;
    if (remind_ != original_.remind) patch.remind = remind_;
    if (tags != original_.tags) patch.tags = tags;
    return patch;
}

std::wstring DetailsDraft::format_due_text(std::optional<Clock::time_point> due_at) {
    if (!due_at) return {};
    constexpr std::int64_t epoch_offset_ms = 11'644'473'600'000LL;
    const auto raw_ticks = (due_at->time_since_epoch().count() + epoch_offset_ms) * 10'000LL;
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<ULONGLONG>(raw_ticks);
    FILETIME utc_file_time{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&utc_file_time, &utc) ||
        !SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) return {};
    wchar_t text[24]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u",
        local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute);
    return text;
}

bool DetailsPanel::create(HWND parent) {
    parent_ = parent;
    heading_ = make_control(L"STATIC", L"任务详情", SS_LEFT, 530);
    note_label_ = make_control(L"STATIC", L"备注", SS_LEFT, 531);
    note_ = make_control(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL |
        ES_WANTRETURN | ES_LEFT | WS_BORDER, 532);
    priority_label_ = make_control(L"STATIC", L"优先级", SS_LEFT, 533);
    priority_ = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 534);
    due_label_ = make_control(L"STATIC", L"截止时间（本地）", SS_LEFT, 535);
    due_ = make_control(L"EDIT", L"", ES_AUTOHSCROLL | ES_LEFT | WS_BORDER, 536);
    tags_label_ = make_control(L"STATIC", L"标签（逗号分隔）", SS_LEFT, 537);
    tags_ = make_control(L"EDIT", L"", ES_AUTOHSCROLL | ES_LEFT | WS_BORDER, 538);
    remind_ = make_control(L"BUTTON", L"开启到期提醒", BS_AUTOCHECKBOX | BS_LEFT, 539);
    save_ = make_control(L"BUTTON", L"保存", BS_PUSHBUTTON, kDetailsSaveControlId);
    cancel_ = make_control(L"BUTTON", L"取消", BS_PUSHBUTTON, kDetailsCancelControlId);
    const std::array controls{heading_, note_label_, note_, priority_label_, priority_,
        due_label_, due_, tags_label_, tags_, remind_, save_, cancel_};
    if (std::any_of(controls.begin(), controls.end(), [](HWND value) { return value == nullptr; })) {
        destroy();
        return false;
    }
    for (const auto child : controls) {
        SendMessageW(child, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        ShowWindow(child, SW_HIDE);
    }
    SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"低"));
    SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"普通"));
    SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"高"));
    SendMessageW(due_, EM_SETLIMITTEXT, 16, 0);
    SendMessageW(tags_, EM_SETLIMITTEXT, 128, 0);
    SendMessageW(note_, EM_SETLIMITTEXT, kMaximumNoteLength, 0);
    return true;
}

void DetailsPanel::destroy() noexcept {
    const std::array controls{heading_, note_label_, note_, priority_label_, priority_,
        due_label_, due_, tags_label_, tags_, remind_, save_, cancel_};
    for (const auto child : controls) if (child != nullptr) DestroyWindow(child);
    parent_ = nullptr;
    heading_ = note_label_ = note_ = priority_label_ = priority_ = nullptr;
    due_label_ = due_ = tags_label_ = tags_ = remind_ = save_ = cancel_ = nullptr;
    task_.reset();
}

void DetailsPanel::open(const Task& task, const RectF& bounds, UINT dpi) {
    task_ = task;
    bounds_ = bounds;
    dpi_ = dpi;
    SetWindowTextW(note_, task.note.c_str());
    SetWindowTextW(due_, DetailsDraft::format_due_text(task.due_at).c_str());
    std::wstring tags;
    for (std::size_t index = 0; index < task.tags.size(); ++index) {
        if (index != 0) tags += L", ";
        tags += task.tags[index];
    }
    SetWindowTextW(tags_, tags.c_str());
    const auto priority_index = task.priority == Priority::low ? 0
        : task.priority == Priority::medium ? 1 : 2;
    SendMessageW(priority_, CB_SETCURSEL, priority_index, 0);
    SendMessageW(remind_, BM_SETCHECK, task.remind ? BST_CHECKED : BST_UNCHECKED, 0);
    layout(bounds, dpi);
    show_controls(true);
    SetFocus(note_);
}

void DetailsPanel::layout(const RectF& bounds, UINT dpi) {
    bounds_ = bounds;
    dpi_ = dpi;
    const auto place = [this, dpi](HWND control, float x, float y, float width, float height) {
        SetWindowPos(control, nullptr, scale_px(bounds_.x + x, dpi),
            scale_px(bounds_.y + y, dpi), std::max(1, scale_px(width, dpi)),
            std::max(1, scale_px(height, dpi)), SWP_NOACTIVATE | SWP_NOZORDER);
    };
    const auto width = std::max(160.0F, bounds.width);
    place(heading_, 4, 2, width - 8, 22);
    place(note_label_, 4, 27, width - 8, 18);
    place(note_, 4, 46, width - 8, 78);
    place(priority_label_, 4, 129, 92, 18);
    place(priority_, 4, 148, 92, 25);
    place(due_label_, 106, 129, width - 110, 18);
    place(due_, 106, 148, width - 110, 25);
    place(tags_label_, 4, 180, width - 8, 18);
    place(tags_, 4, 199, width - 8, 25);
    place(remind_, 4, 231, width - 130, 26);
    place(save_, width - 130, 231, 60, 28);
    place(cancel_, width - 64, 231, 60, 28);
}

void DetailsPanel::close() noexcept {
    show_controls(false);
    task_.reset();
}

bool DetailsPanel::visible() const noexcept { return task_.has_value(); }
const std::wstring& DetailsPanel::task_id() const noexcept {
    static const std::wstring empty;
    return task_ ? task_->id : empty;
}

std::optional<TaskPatch> DetailsPanel::read_patch() {
    if (!task_) return std::nullopt;
    DetailsDraft draft(*task_);
    draft.set_note(read_text(note_));
    const auto selected_priority = SendMessageW(priority_, CB_GETCURSEL, 0, 0);
    if (selected_priority != CB_ERR) {
        draft.set_priority(selected_priority == 0 ? Priority::low
            : selected_priority == 1 ? Priority::medium : Priority::high);
    }
    draft.set_due_text(read_text(due_));
    draft.set_remind(SendMessageW(remind_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    draft.set_tags_text(read_text(tags_));
    return draft.make_patch();
}

HWND DetailsPanel::make_control(
    const wchar_t* class_name, const wchar_t* text, DWORD style, int id) {
    const auto control = CreateWindowExW(0, class_name, text,
        WS_CHILD | WS_TABSTOP | style, 0, 0, 1, 1, parent_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent_, GWLP_HINSTANCE)), nullptr);
    if (control != nullptr) {
        SetWindowSubclass(control, &DetailsPanel::control_proc,
            reinterpret_cast<UINT_PTR>(control), reinterpret_cast<DWORD_PTR>(parent_));
    }
    return control;
}

LRESULT CALLBACK DetailsPanel::control_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference_data) {
    const auto parent = reinterpret_cast<HWND>(reference_data);
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        PostMessageW(parent, kDetailsCancelMessage, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == 'N' &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        PostMessageW(parent, kEditorNewTaskMessage, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == 'F' &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        PostMessageW(parent, kEditorSearchMessage, 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN && (wparam == VK_UP || wparam == VK_DOWN) &&
        (GetKeyState(VK_MENU) & 0x8000) != 0) {
        PostMessageW(parent, kEditorMoveTaskMessage, wparam, 0);
        return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void DetailsPanel::show_controls(bool show) noexcept {
    const std::array controls{heading_, note_label_, note_, priority_label_, priority_,
        due_label_, due_, tags_label_, tags_, remind_, save_, cancel_};
    for (const auto child : controls) {
        if (child != nullptr) ShowWindow(child, show ? SW_SHOWNA : SW_HIDE);
    }
}

}  // namespace desktop_todo
