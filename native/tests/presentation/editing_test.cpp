#include "test_support.h"

#include "presentation/editor_session.h"
#include "presentation/details_panel.h"

using desktop_todo::EditorMode;
using desktop_todo::EditorSession;
using desktop_todo::DetailsDraft;
using desktop_todo::Priority;
using desktop_todo::Task;

TEST_CASE(editing_new_task_commit_trims_title_and_keeps_input_ready) {
    EditorSession editor;
    editor.begin_new_task();
    editor.set_text(L"  写一段 🐈\r\n标题  ");

    const auto result = editor.commit();

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->text, L"写一段 🐈\r\n标题");
    EXPECT_EQ(result->mode, EditorMode::new_task);
    EXPECT_TRUE(editor.active());
    EXPECT_EQ(editor.text(), L"");
}

TEST_CASE(editing_blank_input_is_rejected_without_leaving_session) {
    EditorSession editor;
    editor.begin_new_task();
    editor.set_text(L" \t\r\n ");

    const auto result = editor.commit();

    EXPECT_TRUE(!result.has_value());
    EXPECT_TRUE(editor.active());
    EXPECT_EQ(editor.text(), L" \t\r\n ");
}

TEST_CASE(editing_inline_commit_returns_id_and_title_then_closes) {
    EditorSession editor;
    editor.begin_inline_title(L"task-7", L"旧标题");
    editor.set_text(L"新标题 😀");

    const auto result = editor.commit();

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->target_id, L"task-7");
    EXPECT_EQ(result->text, L"新标题 😀");
    EXPECT_EQ(result->mode, EditorMode::inline_title);
    EXPECT_TRUE(!editor.active());
}

TEST_CASE(editing_cancel_discards_inline_draft) {
    EditorSession editor;
    editor.begin_inline_title(L"task-8", L"保存值");
    editor.set_text(L"未保存");
    editor.cancel();

    EXPECT_TRUE(!editor.active());
    EXPECT_EQ(editor.text(), L"");
}

TEST_CASE(editing_search_commit_preserves_search_focus_and_query) {
    EditorSession editor;
    editor.begin_search(L"原查询");
    editor.set_text(L"中文关键词");

    const auto result = editor.commit();

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->mode, EditorMode::search);
    EXPECT_EQ(result->text, L"中文关键词");
    EXPECT_TRUE(editor.active());
    EXPECT_EQ(editor.text(), L"中文关键词");
}

TEST_CASE(editing_details_patch_preserves_unicode_notes_and_updates_fields) {
    Task task;
    task.id = L"task-detail";
    task.title = L"标题";
    task.note = L"第一行";
    task.priority = Priority::low;
    task.remind = true;
    task.tags = {L"工作"};
    DetailsDraft draft(task);
    draft.set_note(L"中文备注 🐈\r\n第二行");
    draft.set_priority(Priority::high);
    draft.set_due_text(L"2026-12-31 18:45");
    draft.set_remind(false);
    draft.set_tags_text(L"工作, 发布 🚀");

    const auto patch = draft.make_patch();

    EXPECT_TRUE(patch.has_value());
    EXPECT_EQ(patch->note.value(), L"中文备注 🐈\r\n第二行");
    EXPECT_EQ(patch->priority.value(), Priority::high);
    EXPECT_TRUE(patch->due_at.has_value());
    EXPECT_TRUE(patch->due_at->has_value());
    EXPECT_EQ(patch->remind.value(), false);
    EXPECT_EQ(patch->tags.value().size(), std::size_t{2});
}

TEST_CASE(editing_details_rejects_invalid_due_date_without_partial_patch) {
    Task task;
    task.id = L"bad-date";
    DetailsDraft draft(task);
    draft.set_note(L"不能部分保存");
    draft.set_due_text(L"2026-02-31 25:99");

    EXPECT_TRUE(!draft.make_patch().has_value());
}

TEST_CASE(editing_details_rejects_notes_beyond_the_persisted_limit) {
    Task task;
    task.id = L"oversize-note";
    DetailsDraft draft(task);
    draft.set_note(std::wstring(2'001, L'x'));

    EXPECT_TRUE(!draft.make_patch().has_value());
}

TEST_CASE(editing_details_rejects_tags_beyond_the_persisted_limit) {
    Task task;
    task.id = L"oversize-tag";
    DetailsDraft draft(task);
    draft.set_tags_text(std::wstring(25, L'x'));

    EXPECT_TRUE(!draft.make_patch().has_value());
}

TEST_CASE(editing_details_blank_due_date_clears_existing_value) {
    Task task;
    task.id = L"clear-date";
    task.due_at = desktop_todo::Clock::time_point{};
    DetailsDraft draft(task);
    draft.set_due_text(L" ");

    const auto patch = draft.make_patch();

    EXPECT_TRUE(patch.has_value());
    EXPECT_TRUE(patch->due_at.has_value());
    EXPECT_TRUE(!patch->due_at->has_value());
}
