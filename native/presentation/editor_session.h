#pragma once

#include <optional>
#include <string>

namespace desktop_todo {

enum class EditorMode { closed, new_task, inline_title, search };

struct EditorCommit {
    EditorMode mode = EditorMode::closed;
    std::wstring target_id;
    std::wstring text;
};

class EditorSession {
public:
    void begin_new_task();
    void begin_inline_title(std::wstring target_id, std::wstring title);
    void begin_search(std::wstring query);
    void set_text(std::wstring text);
    [[nodiscard]] std::optional<EditorCommit> commit();
    void cancel() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] EditorMode mode() const noexcept;
    [[nodiscard]] const std::wstring& target_id() const noexcept;
    [[nodiscard]] const std::wstring& text() const noexcept;

private:
    EditorMode mode_ = EditorMode::closed;
    std::wstring target_id_;
    std::wstring text_;
};

}  // namespace desktop_todo
