#include "presentation/editor_session.h"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace desktop_todo {
namespace {

std::wstring trim(std::wstring value) {
    const auto not_space = [](wchar_t character) {
        return std::iswspace(static_cast<wint_t>(character)) == 0;
    };
    const auto first = std::find_if(value.begin(), value.end(), not_space);
    if (first == value.end()) return {};
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::wstring{first, last};
}

}  // namespace

void EditorSession::begin_new_task() {
    mode_ = EditorMode::new_task;
    target_id_.clear();
    text_.clear();
}

void EditorSession::begin_inline_title(std::wstring target_id, std::wstring title) {
    mode_ = EditorMode::inline_title;
    target_id_ = std::move(target_id);
    text_ = std::move(title);
}

void EditorSession::begin_search(std::wstring query) {
    mode_ = EditorMode::search;
    target_id_.clear();
    text_ = std::move(query);
}

void EditorSession::set_text(std::wstring text) { text_ = std::move(text); }

std::optional<EditorCommit> EditorSession::commit() {
    if (mode_ == EditorMode::closed) return std::nullopt;
    if (mode_ == EditorMode::search) return EditorCommit{mode_, {}, text_};

    auto normalized = trim(text_);
    if (normalized.empty()) return std::nullopt;
    const auto result = EditorCommit{mode_, target_id_, std::move(normalized)};
    if (mode_ == EditorMode::new_task) {
        text_.clear();
    } else {
        cancel();
    }
    return result;
}

void EditorSession::cancel() noexcept {
    mode_ = EditorMode::closed;
    target_id_.clear();
    text_.clear();
}

bool EditorSession::active() const noexcept { return mode_ != EditorMode::closed; }
EditorMode EditorSession::mode() const noexcept { return mode_; }
const std::wstring& EditorSession::target_id() const noexcept { return target_id_; }
const std::wstring& EditorSession::text() const noexcept { return text_; }

}  // namespace desktop_todo
