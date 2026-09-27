#include "test_support.h"

#include "persistence/state_repository.h"
#include "persistence/json_codec.h"
#include "persistence/fake_file_system.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::LoadStatus;
using desktop_todo::LocalDate;
using desktop_todo::StateRepository;
using desktop_todo::Task;
using desktop_todo::test_support::FakeFileSystem;

const std::filesystem::path root{L"C:/data"};

Task task(std::wstring id, std::wstring title) {
    Task value;
    value.id = std::move(id);
    value.title = std::move(title);
    value.created_at = Clock::time_point{std::chrono::milliseconds{1}};
    value.updated_at = value.created_at;
    return value;
}

AppState state(std::wstring id = L"one") {
    AppState value;
    value.tasks = {task(std::move(id), L"任务")};
    return value;
}

std::wstring key(const std::filesystem::path& path) {
    return path.generic_wstring();
}

class TemporaryDirectory {
public:
    TemporaryDirectory() : path(
        std::filesystem::temp_directory_path() /
        (L"desktop-todo-repository-" + std::to_wstring(
            std::chrono::steady_clock::now().time_since_epoch().count()))) {}
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

}  // namespace

TEST_CASE(state_repository_initial_load_and_orphan_temp_is_ignored) {
    FakeFileSystem files;
    files.files[key(root / L"state.json")] = desktop_todo::encode_state_utf8(state());
    files.files[key(root / L"state.json.tmp")] = desktop_todo::encode_state_utf8(state(L"orphan"));
    StateRepository repository{files, root, [] { return L"20260927-100000"; }};

    const auto loaded = repository.load();

    EXPECT_EQ(loaded.status, LoadStatus::ok);
    EXPECT_EQ(loaded.state.tasks[0].id, L"one");
}

TEST_CASE(state_repository_save_orders_write_flush_replace) {
    FakeFileSystem files;
    files.files[key(root / L"state.json")] = desktop_todo::encode_state_utf8(state(L"old"));
    StateRepository repository{files, root, [] { return L"20260927-100000"; }};

    const auto saved = repository.save(state(L"new"));

    EXPECT_TRUE(saved.ok);
    EXPECT_EQ(files.operations.size(), std::size_t{3});
    EXPECT_EQ(files.operations[0], L"write:state.json.tmp");
    EXPECT_EQ(files.operations[1], L"flush:state.json.tmp");
    EXPECT_EQ(files.operations[2], L"replace:state.json");
    EXPECT_EQ(desktop_todo::decode_state_utf8(files.files[key(root / L"state.json")]).state->tasks[0].id, L"new");
}

TEST_CASE(state_repository_failure_before_replace_retains_old_data) {
    FakeFileSystem files;
    const auto old_data = desktop_todo::encode_state_utf8(state(L"old"));
    files.files[key(root / L"state.json")] = old_data;
    files.fail_operation = 2;
    StateRepository repository{files, root, [] { return L"20260927-100000"; }};

    const auto saved = repository.save(state(L"new"));

    EXPECT_TRUE(!saved.ok);
    EXPECT_EQ(files.files[key(root / L"state.json")], old_data);
}

TEST_CASE(state_repository_corrupt_main_uses_newest_valid_backup_and_preserves_source) {
    FakeFileSystem files;
    files.files[key(root / L"state.json")] = {std::byte{'x'}};
    files.files[key(root / L"backups/2026-09-25.json")] = desktop_todo::encode_state_utf8(state(L"older"));
    files.files[key(root / L"backups/2026-09-26.json")] = desktop_todo::encode_state_utf8(state(L"newer"));
    StateRepository repository{files, root, [] { return L"20260927-100000"; }};

    const auto loaded = repository.load();

    EXPECT_EQ(loaded.status, LoadStatus::restored);
    EXPECT_EQ(loaded.state.tasks[0].id, L"newer");
    EXPECT_TRUE(files.exists(root / L"state.corrupt-20260927-100000.json"));
}

TEST_CASE(state_repository_all_invalid_returns_empty_without_overwrite) {
    FakeFileSystem files;
    const std::vector<std::byte> invalid{std::byte{'x'}};
    files.files[key(root / L"state.json")] = invalid;
    files.files[key(root / L"backups/2026-09-26.json")] = invalid;
    StateRepository repository{files, root, [] { return L"20260927-100000"; }};

    const auto loaded = repository.load();

    EXPECT_EQ(loaded.status, LoadStatus::reset);
    EXPECT_TRUE(loaded.state.tasks.empty());
    EXPECT_TRUE(!files.exists(root / L"state.json"));
    EXPECT_TRUE(files.exists(root / L"state.corrupt-20260927-100000.json"));
}

TEST_CASE(state_repository_daily_backup_is_idempotent_and_keeps_seven_newest_dates) {
    FakeFileSystem files;
    StateRepository repository{files, root, [] { return L"20260927-100000"; }};
    for (int day = 18; day <= 26; ++day) {
        EXPECT_TRUE(repository.ensure_daily_backup(state(std::to_wstring(day)), LocalDate{2026, 9, day}).ok);
    }
    const auto operations_before_repeat = files.operations.size();

    EXPECT_TRUE(repository.ensure_daily_backup(state(L"repeat"), LocalDate{2026, 9, 26}).ok);

    EXPECT_EQ(files.operations.size(), operations_before_repeat);
    const auto backups = files.list(root / L"backups");
    EXPECT_EQ(backups.size(), std::size_t{7});
    EXPECT_TRUE(!files.exists(root / L"backups/2026-09-18.json"));
    EXPECT_TRUE(!files.exists(root / L"backups/2026-09-19.json"));
    EXPECT_TRUE(files.exists(root / L"backups/2026-09-26.json"));
}

TEST_CASE(state_repository_win32_adapter_round_trips_in_temporary_directory) {
    TemporaryDirectory temporary;
    desktop_todo::Win32FileSystem files;
    StateRepository writer{files, temporary.path, [] { return L"20260927-100000"; }};

    EXPECT_TRUE(writer.save(state(L"disk")).ok);
    EXPECT_TRUE(writer.ensure_daily_backup(state(L"disk"), LocalDate{2026, 9, 27}).ok);

    StateRepository reader{files, temporary.path, [] { return L"20260927-100001"; }};
    const auto loaded = reader.load();
    EXPECT_EQ(loaded.status, LoadStatus::ok);
    EXPECT_EQ(loaded.state.tasks[0].id, L"disk");
    EXPECT_TRUE(files.exists(temporary.path / L"backups/2026-09-27.json"));
}
