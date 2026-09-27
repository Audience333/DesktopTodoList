#include "test_support.h"

#include "application/app_service.h"
#include "persistence/json_codec.h"
#include "persistence/fake_file_system.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using desktop_todo::AddTaskCommand;
using desktop_todo::AppEvent;
using desktop_todo::AppEventType;
using desktop_todo::AppService;
using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::ImportMode;
using desktop_todo::LocalDate;
using desktop_todo::QuerySpec;
using desktop_todo::StateRepository;
using desktop_todo::Task;
using desktop_todo::test_support::FakeFileSystem;

const std::filesystem::path root{L"C:/app"};

Task task(std::wstring id, std::wstring title, Clock::time_point now) {
    Task value;
    value.id = std::move(id);
    value.title = std::move(title);
    value.order = 1.0;
    value.created_at = now;
    value.updated_at = now;
    return value;
}

std::wstring key(const std::filesystem::path& path) {
    return path.generic_wstring();
}

struct Harness {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    FakeFileSystem files;
    StateRepository repository{files, root, [] { return L"20260927-120000"; }};
    std::vector<AppEvent> events;
    int next_id = 1;
    AppService service{
        repository,
        [this] { return now; },
        [this] { return L"id-" + std::to_wstring(next_id++); },
        [] { return LocalDate{2026, 9, 27}; },
        [this](const AppEvent& event) { events.push_back(event); }};
};

}  // namespace

TEST_CASE(app_service_starts_from_state_and_creates_daily_backup) {
    Harness harness;
    AppState initial;
    initial.tasks = {task(L"fixture", L"从磁盘加载", harness.now)};
    harness.files.files[key(root / L"state.json")] = desktop_todo::encode_state_utf8(initial);

    const auto started = harness.service.start();

    EXPECT_TRUE(started);
    EXPECT_EQ(harness.service.snapshot().tasks[0].id, L"fixture");
    EXPECT_TRUE(harness.files.exists(root / L"backups/2026-09-27.json"));
    EXPECT_EQ(harness.events[0].type, AppEventType::started);
}

TEST_CASE(app_service_commands_are_dirty_until_forced_flush) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());

    const auto added = harness.service.add_task(AddTaskCommand{.title = L"稍后保存"});

    EXPECT_TRUE(added.has_value());
    EXPECT_TRUE(!harness.files.exists(root / L"state.json"));
    EXPECT_TRUE(harness.service.flush());
    EXPECT_TRUE(harness.files.exists(root / L"state.json"));
    EXPECT_EQ(desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"state.json")]).state->tasks.size(), std::size_t{1});
}

TEST_CASE(app_service_import_requires_acceptance_and_can_export) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"当前"}).has_value());
    AppState incoming;
    incoming.tasks = {task(L"imported", L"导入", harness.now)};

    auto prepared = harness.service.prepare_import(
        desktop_todo::encode_state_utf8(incoming), ImportMode::replace);

    EXPECT_TRUE(prepared.candidate.has_value());
    EXPECT_EQ(harness.service.snapshot().tasks[0].title, L"当前");
    EXPECT_TRUE(harness.service.accept_import(std::move(prepared)));
    EXPECT_EQ(harness.service.snapshot().tasks[0].id, L"imported");
    EXPECT_TRUE(harness.service.export_to(root / L"exports/tasks.json").ok);
    EXPECT_TRUE(harness.files.exists(root / L"exports/tasks.json"));
}

TEST_CASE(app_service_factory_reset_requires_confirmation_and_preserves_backup) {
    Harness harness;
    AppState initial;
    initial.tasks = {task(L"keep", L"重置前", harness.now)};
    harness.files.files[key(root / L"state.json")] = desktop_todo::encode_state_utf8(initial);
    EXPECT_TRUE(harness.service.start());

    EXPECT_TRUE(!harness.service.reset_to_defaults(false));
    EXPECT_EQ(harness.service.snapshot().tasks.size(), std::size_t{1});
    EXPECT_TRUE(harness.service.reset_to_defaults(true));

    EXPECT_TRUE(harness.service.snapshot().tasks.empty());
    EXPECT_TRUE(harness.files.exists(root / L"backups/pre-reset-20260927-120000.json"));
    const auto backup = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"backups/pre-reset-20260927-120000.json")]);
    EXPECT_EQ(backup.state->tasks[0].id, L"keep");
}

TEST_CASE(app_service_reminder_tick_marks_delivered_and_flushes_it) {
    Harness harness;
    AppState initial;
    auto reminder = task(L"due", L"提醒", harness.now - std::chrono::hours{1});
    reminder.due_at = harness.now - std::chrono::minutes{1};
    reminder.remind = true;
    initial.tasks = {reminder};
    harness.files.files[key(root / L"state.json")] = desktop_todo::encode_state_utf8(initial);
    EXPECT_TRUE(harness.service.start());

    const auto batch = harness.service.tick_reminders();

    EXPECT_EQ(batch.items.size(), std::size_t{1});
    EXPECT_EQ(harness.service.snapshot().tasks[0].reminded_at, std::optional{harness.now});
    EXPECT_TRUE(harness.service.flush());
    const auto persisted = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"state.json")]);
    EXPECT_EQ(persisted.state->tasks[0].reminded_at, std::optional{harness.now});
}

TEST_CASE(app_service_save_failure_emits_event_without_state_loss) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"不能丢"}).has_value());
    harness.files.fail_operation = harness.files.operations.size() + 1;

    EXPECT_TRUE(!harness.service.flush());

    EXPECT_EQ(harness.service.snapshot().tasks.size(), std::size_t{1});
    EXPECT_EQ(harness.events.back().type, AppEventType::save_error);
}

TEST_CASE(app_service_query_1000_tasks_meets_release_gate) {
    Harness harness;
    AppState initial;
    for (int index = 0; index < 1'000; ++index) {
        initial.tasks.push_back(task(
            std::to_wstring(index), L"任务 " + std::to_wstring(index), harness.now));
    }
    harness.files.files[key(root / L"state.json")] = desktop_todo::encode_state_utf8(initial);
    EXPECT_TRUE(harness.service.start());
    const auto begin = std::chrono::steady_clock::now();

    const auto visible = harness.service.query(QuerySpec{});
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_EQ(visible.size(), std::size_t{1'000});
    EXPECT_TRUE(elapsed < std::chrono::milliseconds{200});
}
