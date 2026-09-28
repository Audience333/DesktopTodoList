#pragma once

#include "domain/reminder_engine.h"

#include <functional>
#include <string>

namespace desktop_todo {

class AppService;

enum class NotificationChannel { none, toast, tray };

struct NotificationPayload {
    std::wstring title;
    std::wstring body;
};

struct NotificationResult {
    bool delivered = false;
    NotificationChannel channel = NotificationChannel::none;
    std::wstring error;
};

struct NotificationApi {
    std::function<bool(const NotificationPayload&)> toast;
    std::function<bool(const NotificationPayload&)> tray;
};

class NotificationService {
public:
    explicit NotificationService(NotificationApi api = {});

    [[nodiscard]] static NotificationPayload format(const ReminderBatch& batch);
    [[nodiscard]] NotificationResult show_reminders(const ReminderBatch& batch) const;
    [[nodiscard]] NotificationResult deliver(AppService& service, const ReminderBatch& batch) const;

private:
    NotificationApi api_;
};

}  // namespace desktop_todo
