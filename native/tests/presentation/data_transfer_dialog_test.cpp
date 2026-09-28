#include "presentation/data_transfer_dialog.h"

#include "application/app_service.h"
#include "persistence/fake_file_system.h"
#include "tests/test_support.h"

#include <chrono>
#include <filesystem>
#include <string>

using namespace desktop_todo;

namespace {

struct TransferFixture {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    test_support::FakeFileSystem files;
    StateRepository repository{files, L"C:/transfer", [] { return L"20260929-120000"; }};
    int next_id = 0;
    AppService service{repository, [this] { return now; }, [this] {
        return L"generated-" + std::to_wstring(++next_id);
    }, [] { return LocalDate{2026, 9, 29}; }};

    TransferFixture() { EXPECT_TRUE(service.start()); }
};

std::vector<std::byte> source_json(std::string_view json) {
    const auto* first = reinterpret_cast<const std::byte*>(json.data());
    return {first, first + json.size()};
}

}  // namespace

TEST_CASE(data_transfer_dialog_accepts_only_json_drop_paths) {
    EXPECT_TRUE(DataTransferDialog::accepts_json_path(L"C:/tmp/todo.JSON"));
    EXPECT_TRUE(!DataTransferDialog::accepts_json_path(L"C:/tmp/todo.txt"));
}

TEST_CASE(data_transfer_dialog_canceled_import_does_not_mutate_state) {
    TransferFixture fixture;
    const auto before = fixture.service.snapshot();
    const auto source = source_json(R"({"schemaVersion":1,"tasks":[],"settings":{}})");

    const auto result = DataTransferDialog::import_bytes(
        fixture.service, source, std::nullopt, false);

    EXPECT_TRUE(!result.accepted);
    EXPECT_EQ(fixture.service.snapshot().tasks.size(), before.tasks.size());
}

TEST_CASE(data_transfer_dialog_rejects_invalid_dropped_json_without_mutation) {
    TransferFixture fixture;
    const auto before = fixture.service.snapshot();
    const auto source = source_json("not json");

    const auto result = DataTransferDialog::import_bytes(
        fixture.service, source, ImportMode::merge, true);

    EXPECT_TRUE(!result.accepted && !result.error.empty());
    EXPECT_EQ(fixture.service.snapshot().tasks.size(), before.tasks.size());
}

TEST_CASE(data_transfer_dialog_requires_two_confirmations_and_backs_up_reset) {
    TransferFixture fixture;
    EXPECT_TRUE(fixture.service.add_task({.title = L"保留任务"}).has_value());

    EXPECT_TRUE(!DataTransferDialog::reset_with_confirmation(fixture.service, true, false));
    EXPECT_EQ(fixture.service.snapshot().tasks.size(), std::size_t{1});
    EXPECT_TRUE(DataTransferDialog::reset_with_confirmation(fixture.service, true, true));
    EXPECT_TRUE(fixture.service.snapshot().tasks.empty());
    EXPECT_TRUE(fixture.files.exists(
        L"C:/transfer/backups/pre-reset-20260929-120000.json"));
}

TEST_CASE(data_transfer_dialog_reports_export_path_errors) {
    TransferFixture fixture;
    fixture.files.fail_operation = fixture.files.operations.size() + 1;

    const auto result = DataTransferDialog::export_to(
        fixture.service, L"C:/missing/export.json");

    EXPECT_TRUE(!result.ok && !result.error.empty());
}
