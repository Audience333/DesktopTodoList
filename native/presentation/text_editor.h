#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "presentation/editor_session.h"
#include "presentation/layout.h"

#include <optional>
#include <cstdint>

namespace desktop_todo {

enum class NativeEditorControl { none, quick_add, inline_title, search };

inline constexpr UINT kEditorCommitMessage = WM_APP + 0x40;
inline constexpr UINT kEditorCancelMessage = WM_APP + 0x41;
inline constexpr UINT kEditorFocusLostMessage = WM_APP + 0x42;
inline constexpr UINT kEditorNewTaskMessage = WM_APP + 0x43;
inline constexpr UINT kEditorSearchMessage = WM_APP + 0x44;
inline constexpr UINT kEditorMoveTaskMessage = WM_APP + 0x46;

class TextEditor {
public:
    [[nodiscard]] bool create(HWND parent, UINT dpi);
    void destroy() noexcept;
    void layout(const LayoutResult& layout, UINT dpi);
    void place_inline_title(const RectF& bounds, UINT dpi);
    void begin_new_task();
    void begin_inline_title(std::wstring id, std::wstring title);
    void focus_search();
    void close_inline() noexcept;
    void cancel_active() noexcept;
    void clear_search() noexcept;
    [[nodiscard]] std::optional<EditorCommit> commit_active();
    [[nodiscard]] NativeEditorControl control_for(HWND child) const noexcept;
    [[nodiscard]] NativeEditorControl active_control() const noexcept;
    [[nodiscard]] HWND active_handle() const noexcept;
    [[nodiscard]] HWND search_handle() const noexcept;
    [[nodiscard]] const std::wstring& search_text() const noexcept;
    [[nodiscard]] const std::wstring& session_target_id() const noexcept;
    [[nodiscard]] std::uint32_t focus_generation() const noexcept;
    void capture_text(HWND child);

private:
    static LRESULT CALLBACK edit_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR reference_data);
    [[nodiscard]] HWND create_edit(int control_id, DWORD style, const wchar_t* cue);
    void show_active() noexcept;

    HWND parent_ = nullptr;
    HWND quick_add_ = nullptr;
    HWND inline_title_ = nullptr;
    HWND search_ = nullptr;
    UINT dpi_ = 96;
    NativeEditorControl active_ = NativeEditorControl::none;
    EditorSession session_;
    std::wstring search_text_;
};

}  // namespace desktop_todo
