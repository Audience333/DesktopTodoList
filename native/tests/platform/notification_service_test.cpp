#include "platform/windows/notification_service.h"

#include "application/app_service.h"
#include "persistence/fake_file_system.h"
#include "tests/test_support.h"

#include <chrono>
#include <string>

using namespace desktop_todo;

namespace {

struct ReminderFixture {
    Clock::time_point now{std::chrono::milliseconds{1'800'000'000'000}};
    test_support::FakeFileSystem files;
    StateRepository repository{files, L"C:/notifications", [] { return L"test"; }};
    AppService service{repository, [this] { return now; }, [] { return L"task-1"; },
        [] { return LocalDate{2026, 9, 29}; }};

    ReminderFixture() { EXPECT_TRUE(service.start()); }
};

}  // namespace

TEST_CASE(notification_service_aggregates_startup_reminders) {
    ReminderBatch batch;
    batch.startup = true;
    batch.grouped = true;
    batch.items = {{L"1", L"买牛奶", {}, {}, false}, {L"2", L"交报告", {}, {}, true}};

    const auto payload = NotificationService::format(batch);

    EXPECT_TRUE(payload.title.find(L"2") != std::wstring::npos);
    EXPECT_TRUE(payload.body.find(L"买牛奶") != std::wstring::npos);
    EXPECT_TRUE(payload.body.find(L"交报告") != std::wstring::npos);
}

TEST_CASE(notification_service_falls_back_to_tray_after_toast_failure) {
    int toast_calls = 0;
    int tray_calls = 0;
    NotificationService service{{
        [&toast_calls](const NotificationPayload&) { ++toast_calls; return false; },
        [&tray_calls](const NotificationPayload&) { ++tray_calls; return true; }}};
    ReminderBatch batch;
    batch.items = {{L"1", L"交报告", {}, {}, false}};

    const auto result = service.show_reminders(batch);

    EXPECT_TRUE(result.delivered);
    EXPECT_EQ(result.channel, NotificationChannel::tray);
    EXPECT_EQ(toast_calls, 1);
    EXPECT_EQ(tray_calls, 1);
}

TEST_CASE(notification_service_does_not_acknowledge_when_all_channels_fail) {
    ReminderFixture fixture;
    auto task = fixture.service.add_task({.title = L"稍后提醒"});
    EXPECT_TRUE(task.has_value());
    TaskPatch patch;
    patch.due_at = fixture.now;
    patch.remind = true;
    EXPECT_TRUE(fixture.service.update_task(task->id, patch));
    const auto batch = fixture.service.tick_reminders();
    NotificationService service{{
        [](const NotificationPayload&) { return false; },
        [](const NotificationPayload&) { return false; }}};

    const auto result = service.deliver(fixture.service, batch);

    EXPECT_TRUE(!result.delivered);
    EXPECT_TRUE(!fixture.service.snapshot().tasks.front().reminded_at.has_value());
}

