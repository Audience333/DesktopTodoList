#include "test_support.h"

#include "domain/commands.h"
#include "domain/task_query.h"
#include "domain/task_store.h"

#include <chrono>
#include <string>
#include <vector>

namespace {

using desktop_todo::AddTaskCommand;
using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::DropPosition;
using desktop_todo::Priority;
using desktop_todo::QuerySpec;
using desktop_todo::Task;
using desktop_todo::TaskPatch;
using desktop_todo::TaskStatus;
using desktop_todo::TaskStore;
using desktop_todo::ViewKind;

struct StoreFixture {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    TaskStore store{
        AppState{},
        [this] { return now; },
        [this] { return L"generated-" + std::to_wstring(next_id++); }};
};

Task existing_task(std::wstring id, std::wstring title, double order, TaskStatus status = TaskStatus::todo) {
    Task task;
    task.id = std::move(id);
    task.title = std::move(title);
    task.order = order;
    task.status = status;
    task.created_at = Clock::time_point{std::chrono::milliseconds{1'700'000'000'000}};
    task.updated_at = task.created_at;
    if (status == TaskStatus::done) {
        task.completed_at = task.updated_at;
    }
    return task;
}

TaskStore store_with(
    std::vector<Task> tasks,
    Clock::time_point& now,
    int& next_id) {
    AppState state;
    state.tasks = std::move(tasks);
    return TaskStore{
        std::move(state),
        [&now] { return now; },
        [&next_id] { return L"generated-" + std::to_wstring(next_id++); }};
}

}  // namespace

TEST_CASE(task_store_empty_title_is_rejected_without_undo) {
    StoreFixture fixture;

    const auto added = fixture.store.add_task(AddTaskCommand{.title = L"  \t "});

    EXPECT_TRUE(!added.has_value());
    EXPECT_TRUE(fixture.store.state().tasks.empty());
    EXPECT_TRUE(!fixture.store.can_undo());
    EXPECT_TRUE(!fixture.store.take_change().persisted);
}

TEST_CASE(task_store_newest_task_uses_smallest_order) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with(
        {existing_task(L"a", L"A", 2.0), existing_task(L"b", L"B", 5.0)},
        now,
        next_id);

    const auto added = store.add_task(AddTaskCommand{.title = L" 新任务 "});

    EXPECT_TRUE(added.has_value());
    EXPECT_EQ(added->id, L"generated-1");
    EXPECT_EQ(added->title, L"新任务");
    EXPECT_EQ(added->order, 1.0);
    EXPECT_EQ(store.state().tasks.size(), std::size_t{3});
    const auto change = store.take_change();
    EXPECT_TRUE(change.persisted);
    EXPECT_EQ(change.affected_ids, std::vector<std::wstring>{L"generated-1"});
}

TEST_CASE(task_store_add_is_not_undoable_and_invalidates_an_older_undo) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with({existing_task(L"a", L"A", 1.0)}, now, next_id);

    EXPECT_EQ(store.delete_tasks({L"a"}), std::size_t{1});
    EXPECT_TRUE(store.can_undo());
    EXPECT_TRUE(store.add_task(AddTaskCommand{.title = L"B"}).has_value());

    EXPECT_TRUE(!store.can_undo());
    EXPECT_TRUE(!store.undo());
    EXPECT_EQ(store.state().tasks.size(), std::size_t{1});
    EXPECT_EQ(store.state().tasks[0].title, L"B");
}

TEST_CASE(task_store_update_preserves_identity_and_creation_time) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    const auto original = existing_task(L"a", L"A", 1.0);
    auto store = store_with({original}, now, next_id);
    TaskPatch patch;
    patch.id = L"replacement";
    patch.created_at = now;
    patch.title = L"已更新";
    patch.priority = Priority::high;

    const bool updated = store.update_task(L"a", patch);

    EXPECT_TRUE(updated);
    EXPECT_EQ(store.state().tasks[0].id, L"a");
    EXPECT_EQ(store.state().tasks[0].created_at, original.created_at);
    EXPECT_EQ(store.state().tasks[0].title, L"已更新");
    EXPECT_EQ(store.state().tasks[0].priority, Priority::high);
    EXPECT_EQ(store.state().tasks[0].updated_at, now);
}

TEST_CASE(task_store_batch_patch_and_completion_each_undo_as_one_action) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with(
        {existing_task(L"a", L"A", 1.0), existing_task(L"b", L"B", 2.0)},
        now,
        next_id);
    TaskPatch patch;
    patch.priority = Priority::high;
    patch.tags = std::vector<std::wstring>{L"work"};

    EXPECT_EQ(store.update_tasks({L"a", L"b", L"missing"}, patch), std::size_t{2});
    EXPECT_EQ(store.state().tasks[0].priority, Priority::high);
    EXPECT_EQ(store.state().tasks[1].tags, std::vector<std::wstring>{L"work"});
    EXPECT_TRUE(store.undo());
    EXPECT_EQ(store.state().tasks[0].priority, Priority::medium);
    EXPECT_TRUE(store.state().tasks[1].tags.empty());
    EXPECT_TRUE(!store.undo());

    EXPECT_EQ(store.set_completed_tasks({L"a", L"b"}, true), std::size_t{2});
    EXPECT_EQ(store.state().tasks[0].status, TaskStatus::done);
    EXPECT_EQ(store.state().tasks[1].status, TaskStatus::done);
    EXPECT_TRUE(store.undo());
    EXPECT_EQ(store.state().tasks[0].status, TaskStatus::todo);
    EXPECT_EQ(store.state().tasks[1].status, TaskStatus::todo);
    EXPECT_TRUE(!store.undo());
}

TEST_CASE(task_store_completion_sets_and_clears_timestamp) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with({existing_task(L"a", L"A", 1.0)}, now, next_id);

    EXPECT_TRUE(store.set_completed(L"a", true));
    EXPECT_EQ(store.state().tasks[0].status, TaskStatus::done);
    EXPECT_EQ(store.state().tasks[0].completed_at, std::optional{now});

    now += std::chrono::minutes{1};
    EXPECT_TRUE(store.set_completed(L"a", false));
    EXPECT_EQ(store.state().tasks[0].status, TaskStatus::todo);
    EXPECT_TRUE(!store.state().tasks[0].completed_at.has_value());
}

TEST_CASE(task_store_delete_one_and_many) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with(
        {existing_task(L"a", L"A", 1.0), existing_task(L"b", L"B", 2.0),
         existing_task(L"c", L"C", 3.0)},
        now,
        next_id);

    EXPECT_EQ(store.delete_tasks({L"b"}), std::size_t{1});
    EXPECT_EQ(store.state().tasks.size(), std::size_t{2});
    EXPECT_EQ(store.delete_tasks({L"a", L"c", L"missing"}), std::size_t{2});
    EXPECT_TRUE(store.state().tasks.empty());
}

TEST_CASE(task_store_clear_completed_requires_confirmation) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with(
        {existing_task(L"a", L"A", 1.0, TaskStatus::done),
         existing_task(L"b", L"B", 2.0),
         existing_task(L"c", L"C", 3.0, TaskStatus::done)},
        now,
        next_id);

    EXPECT_EQ(store.clear_completed(false), std::size_t{0});
    EXPECT_EQ(store.state().tasks.size(), std::size_t{3});
    EXPECT_EQ(store.clear_completed(true), std::size_t{2});
    EXPECT_EQ(store.state().tasks.size(), std::size_t{1});
    EXPECT_EQ(store.state().tasks[0].id, L"b");
}

TEST_CASE(task_store_undo_succeeds_within_five_seconds) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with({existing_task(L"a", L"A", 1.0)}, now, next_id);

    EXPECT_EQ(store.delete_tasks({L"a"}), std::size_t{1});
    now += std::chrono::milliseconds{4'999};
    EXPECT_TRUE(store.can_undo());
    EXPECT_TRUE(store.undo());
    EXPECT_EQ(store.state().tasks.size(), std::size_t{1});
    EXPECT_EQ(store.state().tasks[0].id, L"a");
    EXPECT_TRUE(!store.can_undo());
}

TEST_CASE(task_store_undo_expires_after_five_seconds) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with({existing_task(L"a", L"A", 1.0)}, now, next_id);

    EXPECT_EQ(store.delete_tasks({L"a"}), std::size_t{1});
    now += std::chrono::seconds{5};
    EXPECT_TRUE(!store.can_undo());
    EXPECT_TRUE(!store.undo());
    EXPECT_TRUE(store.state().tasks.empty());
}

TEST_CASE(task_store_only_latest_action_is_undoable) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with(
        {existing_task(L"a", L"A", 1.0), existing_task(L"b", L"B", 2.0)},
        now,
        next_id);

    EXPECT_TRUE(store.set_completed(L"a", true));
    EXPECT_EQ(store.delete_tasks({L"b"}), std::size_t{1});
    EXPECT_TRUE(store.undo());
    EXPECT_EQ(store.state().tasks.size(), std::size_t{2});
    EXPECT_EQ(store.state().tasks[0].status, TaskStatus::done);
    EXPECT_TRUE(!store.undo());
}

TEST_CASE(task_store_schedule_edit_resets_reminder_delivery) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto value = existing_task(L"a", L"A", 1.0);
    value.due_at = now;
    value.reminded_at = now;
    auto store = store_with({value}, now, next_id);
    TaskPatch patch;
    patch.due_at = now + std::chrono::hours{1};

    EXPECT_TRUE(store.update_task(L"a", patch));
    EXPECT_TRUE(!store.state().tasks[0].reminded_at.has_value());
}

TEST_CASE(task_store_undo_preserves_unrelated_reminder_delivery) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto first = existing_task(L"a", L"A", 1.0);
    auto second = existing_task(L"b", L"B", 2.0);
    second.due_at = now;
    auto store = store_with({first, second}, now, next_id);
    TaskPatch patch;
    patch.title = L"changed";
    EXPECT_TRUE(store.update_task(L"a", patch));
    EXPECT_TRUE(store.mark_reminded({L"b"}, now));

    EXPECT_TRUE(store.undo());

    EXPECT_EQ(store.state().tasks[0].title, L"A");
    EXPECT_EQ(store.state().tasks[1].reminded_at, std::optional{now});
}

TEST_CASE(task_store_reorder_uses_manual_order_not_storage_order) {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    int next_id = 1;
    auto store = store_with(
        {existing_task(L"new", L"New", -1.0), existing_task(L"old", L"Old", 1.0),
         existing_task(L"middle", L"Middle", 0.0)},
        now,
        next_id);

    EXPECT_TRUE(store.reorder(L"old", L"new", DropPosition::before));

    const auto ordered = desktop_todo::query_tasks(
        store.state(), QuerySpec{.view = ViewKind::all}, now);
    EXPECT_EQ(ordered[0].task->id, L"old");
    EXPECT_EQ(ordered[1].task->id, L"new");
    EXPECT_EQ(ordered[2].task->id, L"middle");
}
