#include "test_support.h"

#include "persistence/import_export.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::ImportMode;
using desktop_todo::Task;

std::vector<std::byte> bytes(std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

Task task(std::wstring id, std::wstring title, std::int64_t updated) {
    Task value;
    value.id = std::move(id);
    value.title = std::move(title);
    value.created_at = Clock::time_point{std::chrono::milliseconds{1}};
    value.updated_at = Clock::time_point{std::chrono::milliseconds{updated}};
    return value;
}

}  // namespace

TEST_CASE(import_export_replace_failure_leaves_current_state_untouched) {
    AppState current;
    current.tasks = {task(L"keep", L"保留", 10)};

    const auto result = desktop_todo::prepare_import(
        current, bytes("{broken"), ImportMode::replace);

    EXPECT_TRUE(!result.candidate.has_value());
    EXPECT_TRUE(!result.error.empty());
    EXPECT_EQ(current.tasks.size(), std::size_t{1});
    EXPECT_EQ(current.tasks[0].title, L"保留");
}

TEST_CASE(import_export_replace_returns_candidate_without_mutating_current) {
    AppState current;
    current.tasks = {task(L"old", L"旧任务", 10)};
    const auto incoming = desktop_todo::encode_state_utf8(AppState{
        .schema_version = 1,
        .tasks = {task(L"new", L"新任务", 20)}});

    const auto result = desktop_todo::prepare_import(
        current, incoming, ImportMode::replace);

    EXPECT_TRUE(result.candidate.has_value());
    EXPECT_EQ(result.candidate->tasks.size(), std::size_t{1});
    EXPECT_EQ(result.candidate->tasks[0].id, L"new");
    EXPECT_EQ(current.tasks[0].id, L"old");
}

TEST_CASE(import_export_merge_adds_ids_and_uses_newer_duplicate) {
    AppState current;
    current.tasks = {
        task(L"same", L"当前较新", 30),
        task(L"keep", L"不相关", 10)};
    AppState incoming_state;
    incoming_state.tasks = {
        task(L"same", L"导入较旧", 20),
        task(L"added", L"新增", 40)};
    auto result = desktop_todo::prepare_import(
        current,
        desktop_todo::encode_state_utf8(incoming_state),
        ImportMode::merge);

    EXPECT_TRUE(result.candidate.has_value());
    EXPECT_EQ(result.added, std::size_t{1});
    EXPECT_EQ(result.candidate->tasks.size(), std::size_t{3});
    EXPECT_EQ(result.candidate->tasks[0].title, L"当前较新");
    EXPECT_EQ(result.candidate->tasks[1].title, L"不相关");

    incoming_state.tasks[0] = task(L"same", L"导入较新", 50);
    result = desktop_todo::prepare_import(
        current,
        desktop_todo::encode_state_utf8(incoming_state),
        ImportMode::merge);
    EXPECT_EQ(result.candidate->tasks[0].title, L"导入较新");
    EXPECT_EQ(result.candidate->tasks[1].id, L"keep");
}
