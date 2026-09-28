#pragma once

#include "domain/commands.h"
#include "presentation/layout.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace desktop_todo {

class DetailsDraft {
public:
    explicit DetailsDraft(const Task& task);

    void set_note(std::wstring note);
    void set_priority(Priority priority) noexcept;
    void set_due_text(std::wstring local_date_time);
    void set_remind(bool remind) noexcept;
    void set_tags_text(std::wstring comma_separated_tags);

    [[nodiscard]] std::optional<TaskPatch> make_patch() const;
    [[nodiscard]] static std::wstring format_due_text(
        std::optional<Clock::time_point> due_at);

private:
    Task original_;
    std::wstring note_;
    Priority priority_ = Priority::medium;
    std::wstring due_text_;
    bool remind_ = true;
    std::wstring tags_text_;
};

inline constexpr int kDetailsSaveControlId = 521;
inline constexpr int kDetailsCancelControlId = 522;
inline constexpr UINT kDetailsCancelMessage = WM_APP + 0x45;

class DetailsPanel {
public:
    [[nodiscard]] bool create(HWND parent);
    void destroy() noexcept;
    void open(const Task& task, const RectF& bounds, UINT dpi);
    void layout(const RectF& bounds, UINT dpi);
    void close() noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] const std::wstring& task_id() const noexcept;
    [[nodiscard]] std::optional<TaskPatch> read_patch();

private:
    static LRESULT CALLBACK control_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR reference_data);
    HWND make_control(const wchar_t* class_name, const wchar_t* text,
        DWORD style, int id);
    void show_controls(bool show) noexcept;

    HWND parent_ = nullptr;
    HWND heading_ = nullptr;
    HWND note_label_ = nullptr;
    HWND note_ = nullptr;
    HWND priority_label_ = nullptr;
    HWND priority_ = nullptr;
    HWND due_label_ = nullptr;
    HWND due_ = nullptr;
    HWND tags_label_ = nullptr;
    HWND tags_ = nullptr;
    HWND remind_ = nullptr;
    HWND save_ = nullptr;
    HWND cancel_ = nullptr;
    std::optional<Task> task_;
    RectF bounds_;
    UINT dpi_ = 96;
};

}  // namespace desktop_todo
