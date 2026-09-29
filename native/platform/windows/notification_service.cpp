#include "platform/windows/notification_service.h"

#include "application/app_service.h"
#include "platform/windows/tray_icon.h"

#include <shlobj.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include <algorithm>
#include <mutex>
#include <string>

namespace desktop_todo {
namespace {

std::wstring escape_xml(std::wstring_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case L'&': result += L"&amp;"; break;
        case L'<': result += L"&lt;"; break;
        case L'>': result += L"&gt;"; break;
        case L'"': result += L"&quot;"; break;
        case L'\'': result += L"&apos;"; break;
        default: result.push_back(character); break;
        }
    }
    return result;
}

bool show_native_toast(const NotificationPayload& payload) {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        try { winrt::init_apartment(winrt::apartment_type::single_threaded); }
        catch (...) { }
    });
    try {
        static_cast<void>(SetCurrentProcessExplicitAppUserModelID(L"DesktopTodoList.Native"));
        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;
        XmlDocument document;
        const auto xml = L"<toast><visual><binding template=\"ToastGeneric\"><text>" +
            escape_xml(payload.title) + L"</text><text>" + escape_xml(payload.body) +
            L"</text></binding></visual></toast>";
        document.LoadXml(xml);
        ToastNotificationManager::CreateToastNotifier().Show(ToastNotification{document});
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

NotificationService::NotificationService(NotificationApi api) : api_(std::move(api)) {
    if (!api_.toast) api_.toast = show_native_toast;
}

NotificationPayload NotificationService::format(const ReminderBatch& batch) {
    NotificationPayload payload;
    if (batch.startup && batch.items.size() > 1) {
        payload.title = L"启动时有 " + std::to_wstring(batch.items.size()) + L" 项待办提醒";
        payload.body = L"以下任务需要关注：";
    } else {
        payload.title = batch.startup ? L"启动时的待办提醒" : L"待办提醒";
    }
    const auto count = std::min<std::size_t>(batch.items.size(), 5);
    for (std::size_t index = 0; index < count; ++index) {
        if (!payload.body.empty()) payload.body += L"\n";
        payload.body += batch.items[index].title;
        if (batch.items[index].overdue) payload.body += L"（已逾期）";
    }
    if (batch.items.size() > count) {
        payload.body += L"\n还有 " + std::to_wstring(batch.items.size() - count) + L" 项…";
    }
    return payload;
}

NotificationResult NotificationService::show_reminders(const ReminderBatch& batch) const {
    if (batch.items.empty()) return {false, NotificationChannel::none, L"没有待通知的任务。"};
    const auto payload = format(batch);
    if (api_.toast && api_.toast(payload)) return {true, NotificationChannel::toast, {}};
    if (api_.tray && api_.tray(payload)) return {true, NotificationChannel::tray, {}};
    return {false, NotificationChannel::none, L"系统通知和托盘通知均未能显示。"};
}

NotificationResult NotificationService::deliver(
    AppService& service, const ReminderBatch& batch) const {
    auto result = show_reminders(batch);
    if (result.delivered) static_cast<void>(service.acknowledge_reminders(batch));
    return result;
}

}  // namespace desktop_todo
