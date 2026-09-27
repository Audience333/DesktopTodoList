#include "test_support.h"

#include "domain/types.h"
#include "domain/validation.h"

#include <chrono>
#include <string>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::Priority;
using desktop_todo::Task;
using desktop_todo::TaskStatus;
using desktop_todo::Theme;
using desktop_todo::WindowLayer;

constexpr auto fixed_now = Clock::time_point{std::chrono::milliseconds{1'800'000'000'000}};

Task valid_task() {
    Task task;
    task.id = L"task-1";
    task.title = L"提交周报";
    task.created_at = fixed_now - std::chrono::hours{1};
    task.updated_at = task.created_at;
    return task;
}

}  // namespace

TEST_CASE(validation_blank_title_is_rejected) {
    auto task = valid_task();
    task.title = L" \t\r\n ";

    const auto result = desktop_todo::validate_task(task, 7.0, fixed_now);

    EXPECT_TRUE(!result.task.has_value());
    EXPECT_TRUE(!result.issues.empty());
}

TEST_CASE(validation_title_is_trimmed_and_limited_to_200) {
    auto task = valid_task();
    const auto original_created_at = task.created_at;
    task.title = L"  " + std::wstring(205, L'x') + L"  ";

    const auto result = desktop_todo::validate_task(task, 7.0, fixed_now);

    EXPECT_TRUE(result.task.has_value());
    EXPECT_EQ(result.task->title.size(), std::size_t{200});
    EXPECT_EQ(result.task->id, L"task-1");
    EXPECT_EQ(result.task->created_at, original_created_at);
    EXPECT_TRUE(!result.issues.empty());
}

TEST_CASE(validation_tags_are_unique_and_limited_to_5) {
    auto task = valid_task();
    task.tags = {L" x ", L"x", L"y", L"z", L"w", L"v", L"u"};

    const auto result = desktop_todo::validate_task(task, 1.0, fixed_now);

    EXPECT_TRUE(result.task.has_value());
    EXPECT_EQ(result.task->tags.size(), std::size_t{5});
    EXPECT_EQ(result.task->tags[0], L"x");
    EXPECT_EQ(result.task->tags[4], L"v");
    EXPECT_TRUE(!result.issues.empty());
}

TEST_CASE(validation_todo_clears_completed_at) {
    auto task = valid_task();
    task.status = TaskStatus::todo;
    task.completed_at = fixed_now;

    const auto result = desktop_todo::validate_task(task, 1.0, fixed_now);

    EXPECT_TRUE(result.task.has_value());
    EXPECT_TRUE(!result.task->completed_at.has_value());
}

TEST_CASE(validation_done_supplies_completed_at) {
    auto task = valid_task();
    task.status = TaskStatus::done;
    task.completed_at.reset();

    const auto result = desktop_todo::validate_task(task, 1.0, fixed_now);

    EXPECT_TRUE(result.task.has_value());
    EXPECT_EQ(result.task->completed_at, std::optional{fixed_now});
}

TEST_CASE(validation_invalid_enums_use_documented_defaults) {
    auto task = valid_task();
    task.priority = static_cast<Priority>(99);
    task.status = static_cast<TaskStatus>(99);
    AppState state;
    state.schema_version = 1;
    state.tasks = {task};
    state.settings.theme = static_cast<Theme>(99);
    state.settings.window_layer = static_cast<WindowLayer>(99);
    state.settings.week_starts_on = 9;

    const auto result = desktop_todo::validate_state(state, fixed_now);

    EXPECT_TRUE(result.supported);
    EXPECT_EQ(result.state.tasks[0].priority, Priority::medium);
    EXPECT_EQ(result.state.tasks[0].status, TaskStatus::todo);
    EXPECT_EQ(result.state.settings.theme, Theme::system);
    EXPECT_EQ(result.state.settings.window_layer, WindowLayer::normal);
    EXPECT_EQ(result.state.settings.week_starts_on, 1);
    EXPECT_TRUE(!result.issues.empty());
}

TEST_CASE(validation_unknown_schema_reports_unsupported) {
    AppState state;
    state.schema_version = 99;

    const auto result = desktop_todo::validate_state(state, fixed_now);

    EXPECT_TRUE(!result.supported);
    EXPECT_TRUE(!result.issues.empty());
}
