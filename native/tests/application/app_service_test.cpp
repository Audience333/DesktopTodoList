#include "test_support.h"

#include "application/app_service.h"
#include "persistence/json_codec.h"
#include "persistence/fake_file_system.h"

#include <algorithm>
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
    LocalDate today{2026, 9, 27};
    FakeFileSystem files;
    StateRepository repository{files, root, [] { return L"20260927-120000"; }};
    std::vector<AppEvent> events;
    int next_id = 1;
    AppService service{
        repository,
        [this] { return now; },
        [this] { return L"id-" + std::to_wstring(next_id++); },
        [this] { return today; },
        [this](const AppEvent& event) { events.push_back(event); }};
};

}  // namespace

TEST_CASE(app_service_starts_from_state_and_creates_daily_backup) {
    Harness harness;
    AppState initial;
    initial.tasks = {task(L"fixture", L"从磁盘加载", harness.now)};
    harness.files.files[key(root / L"data.json")] = desktop_todo::encode_state_utf8(initial);

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
    EXPECT_TRUE(!harness.files.exists(root / L"data.json"));
    EXPECT_TRUE(harness.service.flush());
    EXPECT_TRUE(harness.files.exists(root / L"data.json"));
    EXPECT_EQ(desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"data.json")]).state->tasks.size(), std::size_t{1});
}

TEST_CASE(app_service_batch_mutations_are_single_undoable_commands) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    AppState initial;
    initial.tasks = {task(L"a", L"A", harness.now), task(L"b", L"B", harness.now)};
    auto prepared = harness.service.prepare_import(
        desktop_todo::encode_state_utf8(initial), ImportMode::replace);
    EXPECT_TRUE(harness.service.accept_import(std::move(prepared)));

    desktop_todo::TaskPatch patch;
    patch.priority = desktop_todo::Priority::high;
    patch.tags = std::vector<std::wstring>{L"work"};
    EXPECT_EQ(harness.service.update_tasks({L"a", L"b"}, patch), std::size_t{2});
    EXPECT_TRUE(harness.service.can_undo());
    EXPECT_TRUE(harness.service.undo());
    EXPECT_EQ(harness.service.snapshot().tasks[0].priority, desktop_todo::Priority::medium);
    EXPECT_TRUE(harness.service.snapshot().tasks[1].tags.empty());
    EXPECT_TRUE(!harness.service.undo());

    EXPECT_EQ(harness.service.set_completed_tasks({L"a", L"b"}, true), std::size_t{2});
    EXPECT_TRUE(harness.service.undo());
    EXPECT_EQ(harness.service.snapshot().tasks[0].status, desktop_todo::TaskStatus::todo);
    EXPECT_EQ(harness.service.snapshot().tasks[1].status, desktop_todo::TaskStatus::todo);
    EXPECT_TRUE(!harness.service.undo());

    EXPECT_EQ(harness.service.delete_tasks({L"a", L"b"}), std::size_t{2});
    EXPECT_TRUE(harness.service.undo());
    EXPECT_EQ(harness.service.snapshot().tasks.size(), std::size_t{2});
    EXPECT_TRUE(!harness.service.undo());
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
    harness.files.files[key(root / L"data.json")] = desktop_todo::encode_state_utf8(initial);
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
    harness.files.files[key(root / L"data.json")] = desktop_todo::encode_state_utf8(initial);
    EXPECT_TRUE(harness.service.start());

    const auto batch = harness.service.tick_reminders();

    EXPECT_EQ(batch.items.size(), std::size_t{1});
    EXPECT_TRUE(!harness.service.snapshot().tasks[0].reminded_at.has_value());
    EXPECT_TRUE(harness.service.acknowledge_reminders(batch));
    EXPECT_EQ(harness.service.snapshot().tasks[0].reminded_at, std::optional{harness.now});
    EXPECT_TRUE(harness.service.flush());
    const auto persisted = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"data.json")]);
    EXPECT_EQ(persisted.state->tasks[0].reminded_at, std::optional{harness.now});
}

TEST_CASE(app_service_start_emits_grouped_startup_reminders_without_acknowledging) {
    Harness harness;
    AppState initial;
    auto first = task(L"one", L"一", harness.now - std::chrono::hours{1});
    auto second = task(L"two", L"二", harness.now - std::chrono::hours{1});
    first.due_at = harness.now - std::chrono::minutes{1};
    second.due_at = harness.now - std::chrono::minutes{2};
    initial.tasks = {first, second};
    harness.files.files[key(root / L"data.json")] = desktop_todo::encode_state_utf8(initial);

    EXPECT_TRUE(harness.service.start());

    const auto reminder_event = std::find_if(
        harness.events.begin(), harness.events.end(), [](const AppEvent& event) {
            return event.type == AppEventType::reminders;
        });
    EXPECT_TRUE(reminder_event != harness.events.end());
    EXPECT_TRUE(reminder_event->reminder_batch.startup);
    EXPECT_TRUE(reminder_event->reminder_batch.grouped);
    EXPECT_TRUE(!harness.service.snapshot().tasks[0].reminded_at.has_value());
}

TEST_CASE(app_service_debounced_maintenance_saves_after_deadline) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"自动保存"}).has_value());

    harness.now += std::chrono::milliseconds{499};
    EXPECT_TRUE(harness.service.maintenance());
    EXPECT_TRUE(!harness.files.exists(root / L"data.json"));
    harness.now += std::chrono::milliseconds{1};
    EXPECT_TRUE(harness.service.maintenance());
    EXPECT_TRUE(harness.files.exists(root / L"data.json"));
}

TEST_CASE(app_service_maintenance_creates_backup_after_day_rollover) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    harness.today = LocalDate{2026, 9, 28};

    EXPECT_TRUE(harness.service.maintenance());

    EXPECT_TRUE(harness.files.exists(root / L"backups/2026-09-28.json"));
}

TEST_CASE(app_service_replace_import_creates_current_state_backup_and_aborts_on_failure) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"当前"}).has_value());
    AppState incoming;
    incoming.tasks = {task(L"new", L"新", harness.now)};
    auto prepared = harness.service.prepare_import(
        desktop_todo::encode_state_utf8(incoming), ImportMode::replace);

    EXPECT_TRUE(harness.service.accept_import(std::move(prepared)));
    EXPECT_TRUE(harness.files.exists(root / L"backups/pre-import-20260927-120000.json"));
    const auto backup = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"backups/pre-import-20260927-120000.json")]);
    EXPECT_EQ(backup.state->tasks[0].title, L"当前");

    auto second = harness.service.prepare_import(
        desktop_todo::encode_state_utf8(AppState{}), ImportMode::replace);
    harness.files.fail_operation = harness.files.operations.size() + 1;
    EXPECT_TRUE(!harness.service.accept_import(std::move(second)));
    EXPECT_EQ(harness.service.snapshot().tasks[0].id, L"new");
}

TEST_CASE(app_service_reports_recovery_status) {
    Harness harness;
    harness.files.files[key(root / L"data.json")] = {std::byte{'x'}};
    AppState backup;
    backup.tasks = {task(L"safe", L"恢复", harness.now)};
    harness.files.files[key(root / L"backups/2026-09-26.json")] = desktop_todo::encode_state_utf8(backup);

    EXPECT_TRUE(harness.service.start());

    EXPECT_TRUE(std::any_of(harness.events.begin(), harness.events.end(), [](const AppEvent& event) {
        return event.type == AppEventType::recovery;
    }));
}

TEST_CASE(app_service_blocks_writes_until_corrupt_source_can_be_preserved) {
    Harness harness;
    const std::vector<std::byte> corrupt{std::byte{'x'}};
    harness.files.files[key(root / L"data.json")] = corrupt;
    harness.files.fail_operation = 1;

    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"待恢复"}).has_value());
    EXPECT_TRUE(!harness.service.flush());
    EXPECT_EQ(harness.files.files[key(root / L"data.json")], corrupt);
    EXPECT_TRUE(!harness.files.exists(root / L"backups/2026-09-27.json"));

    harness.files.fail_operation.reset();
    harness.now += std::chrono::milliseconds{500};
    EXPECT_TRUE(harness.service.maintenance());
    EXPECT_TRUE(harness.files.exists(root / L"state.corrupt-20260927-120000.json"));
    const auto saved = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"data.json")]);
    EXPECT_EQ(saved.state->tasks[0].title, L"待恢复");
}

TEST_CASE(app_service_recovery_retry_restores_backup_and_preserves_blocked_edits) {
    Harness harness;
    const std::vector<std::byte> corrupt{std::byte{'x'}};
    harness.files.files[key(root / L"data.json")] = corrupt;
    AppState backup;
    backup.settings.theme = desktop_todo::Theme::dark;
    backup.tasks = {task(L"safe", L"备份任务", harness.now - std::chrono::hours{1})};
    harness.files.files[key(root / L"backups/2026-09-26.json")] =
        desktop_todo::encode_state_utf8(backup);
    harness.files.fail_operation = 1;

    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"阻塞期新任务"}).has_value());

    harness.files.fail_operation.reset();
    harness.now += std::chrono::milliseconds{500};
    EXPECT_TRUE(harness.service.maintenance());

    EXPECT_EQ(harness.service.snapshot().tasks.size(), std::size_t{2});
    EXPECT_EQ(harness.service.snapshot().settings.theme, desktop_todo::Theme::dark);
    EXPECT_TRUE(std::any_of(
        harness.service.snapshot().tasks.begin(), harness.service.snapshot().tasks.end(),
        [](const Task& value) { return value.id == L"safe"; }));
    EXPECT_TRUE(std::any_of(
        harness.service.snapshot().tasks.begin(), harness.service.snapshot().tasks.end(),
        [](const Task& value) { return value.title == L"阻塞期新任务"; }));

    const auto daily = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"backups/2026-09-27.json")]);
    EXPECT_EQ(daily.state->tasks.size(), std::size_t{2});
    const auto saved = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"data.json")]);
    EXPECT_EQ(saved.state->tasks.size(), std::size_t{2});
}

TEST_CASE(app_service_recovery_retry_without_edits_backs_up_recovered_state) {
    Harness harness;
    harness.files.files[key(root / L"data.json")] = {std::byte{'x'}};
    AppState backup;
    backup.tasks = {task(L"safe", L"不应丢失", harness.now)};
    harness.files.files[key(root / L"backups/2026-09-26.json")] =
        desktop_todo::encode_state_utf8(backup);
    harness.files.fail_operation = 1;

    EXPECT_TRUE(harness.service.start());
    harness.files.fail_operation.reset();
    EXPECT_TRUE(harness.service.maintenance());

    EXPECT_EQ(harness.service.snapshot().tasks.size(), std::size_t{1});
    EXPECT_EQ(harness.service.snapshot().tasks[0].id, L"safe");
    const auto daily = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"backups/2026-09-27.json")]);
    EXPECT_EQ(daily.state->tasks.size(), std::size_t{1});
    EXPECT_EQ(daily.state->tasks[0].id, L"safe");
}

TEST_CASE(app_service_recovery_retry_preserves_replace_import_intent) {
    Harness harness;
    harness.files.files[key(root / L"data.json")] = {std::byte{'x'}};
    AppState backup;
    backup.settings.theme = desktop_todo::Theme::dark;
    backup.tasks = {task(L"old", L"旧备份", harness.now)};
    harness.files.files[key(root / L"backups/2026-09-26.json")] =
        desktop_todo::encode_state_utf8(backup);
    harness.files.fail_operation = 1;
    EXPECT_TRUE(harness.service.start());

    AppState replacement;
    replacement.settings.theme = desktop_todo::Theme::light;
    replacement.tasks = {task(L"new", L"覆盖导入", harness.now)};
    auto prepared = harness.service.prepare_import(
        desktop_todo::encode_state_utf8(replacement), ImportMode::replace);
    harness.files.fail_operation.reset();
    EXPECT_TRUE(harness.service.accept_import(std::move(prepared)));
    EXPECT_TRUE(harness.service.maintenance());

    EXPECT_EQ(harness.service.snapshot().tasks.size(), std::size_t{1});
    EXPECT_EQ(harness.service.snapshot().tasks[0].id, L"new");
    EXPECT_EQ(harness.service.snapshot().settings.theme, desktop_todo::Theme::light);
}

TEST_CASE(app_service_recovery_retry_preserves_factory_reset_intent) {
    Harness harness;
    harness.files.files[key(root / L"data.json")] = {std::byte{'x'}};
    AppState backup;
    backup.tasks = {task(L"old", L"旧备份", harness.now)};
    harness.files.files[key(root / L"backups/2026-09-26.json")] =
        desktop_todo::encode_state_utf8(backup);
    harness.files.fail_operation = 1;
    EXPECT_TRUE(harness.service.start());

    harness.files.fail_operation.reset();
    EXPECT_TRUE(harness.service.reset_to_defaults(true));
    EXPECT_TRUE(harness.service.maintenance());

    EXPECT_TRUE(harness.service.snapshot().tasks.empty());
    const auto daily = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"backups/2026-09-27.json")]);
    EXPECT_TRUE(daily.state->tasks.empty());
}

TEST_CASE(app_service_backup_failure_does_not_block_due_main_save) {
    Harness harness;
    EXPECT_TRUE(harness.service.start());
    EXPECT_TRUE(harness.service.add_task(AddTaskCommand{.title = L"仍需保存"}).has_value());
    harness.today = LocalDate{2026, 9, 28};
    harness.now += std::chrono::milliseconds{500};
    harness.files.fail_operation = harness.files.operations.size() + 1;

    EXPECT_TRUE(!harness.service.maintenance());

    EXPECT_TRUE(harness.files.exists(root / L"data.json"));
    const auto saved = desktop_todo::decode_state_utf8(
        harness.files.files[key(root / L"data.json")]);
    EXPECT_EQ(saved.state->tasks[0].title, L"仍需保存");
}

TEST_CASE(app_service_ignores_stale_reminder_acknowledgment_after_schedule_edit) {
    Harness harness;
    AppState initial;
    auto reminder = task(L"due", L"提醒", harness.now - std::chrono::hours{1});
    reminder.due_at = harness.now - std::chrono::minutes{1};
    initial.tasks = {reminder};
    harness.files.files[key(root / L"data.json")] = desktop_todo::encode_state_utf8(initial);
    EXPECT_TRUE(harness.service.start());
    const auto old_delivery = harness.service.tick_reminders();
    desktop_todo::TaskPatch patch;
    patch.due_at = harness.now + std::chrono::hours{1};
    EXPECT_TRUE(harness.service.update_task(L"due", patch));

    EXPECT_TRUE(!harness.service.acknowledge_reminders(old_delivery));

    EXPECT_TRUE(!harness.service.snapshot().tasks[0].reminded_at.has_value());
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
    harness.files.files[key(root / L"data.json")] = desktop_todo::encode_state_utf8(initial);
    EXPECT_TRUE(harness.service.start());
    const auto begin = std::chrono::steady_clock::now();

    const auto visible = harness.service.query(QuerySpec{});
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_EQ(visible.size(), std::size_t{1'000});
    EXPECT_TRUE(elapsed < std::chrono::milliseconds{200});
}
