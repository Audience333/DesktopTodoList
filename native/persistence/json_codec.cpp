#include "persistence/json_codec.h"

#include "domain/validation.h"

#include <Windows.h>
#include <roapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace desktop_todo {
namespace {

using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;

void ensure_apartment() {
    thread_local const bool initialized = [] {
        const auto result = RoInitialize(RO_INIT_MULTITHREADED);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
            winrt::check_hresult(result);
        }
        return true;
    }();
    (void)initialized;
}

std::wstring strict_wide(std::span<const std::byte> source) {
    if (source.empty()) {
        throw std::invalid_argument{"empty JSON"};
    }
    const auto* bytes = reinterpret_cast<const char*>(source.data());
    const auto size = static_cast<int>(source.size());
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, nullptr, 0);
    if (length == 0) {
        throw std::invalid_argument{"invalid UTF-8"};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, result.data(), length) == 0) {
        throw std::invalid_argument{"invalid UTF-8"};
    }
    return result;
}

std::vector<std::byte> strict_utf8(std::wstring_view source) {
    const auto length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()),
        nullptr, 0, nullptr, nullptr);
    if (length == 0 && !source.empty()) {
        throw std::invalid_argument{"invalid UTF-16"};
    }
    std::vector<std::byte> result(static_cast<std::size_t>(length));
    if (length > 0 && WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), static_cast<int>(source.size()),
        reinterpret_cast<char*>(result.data()), length, nullptr, nullptr) == 0) {
        throw std::invalid_argument{"invalid UTF-16"};
    }
    return result;
}

std::optional<Clock::time_point> parse_time(const JsonObject& object, std::wstring_view name) {
    const winrt::hstring key{name};
    if (!object.HasKey(key) || object.GetNamedValue(key).ValueType() ==
        winrt::Windows::Data::Json::JsonValueType::Null) {
        return std::nullopt;
    }
    const auto value = std::wstring{object.GetNamedString(key)};
    if (value.size() != 24 || value[4] != L'-' || value[7] != L'-' ||
        value[10] != L'T' || value[13] != L':' || value[16] != L':' ||
        value[19] != L'.' || value[23] != L'Z') {
        throw std::invalid_argument{"invalid UTC timestamp"};
    }
    for (const auto index : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21, 22}) {
        if (value[static_cast<std::size_t>(index)] < L'0' ||
            value[static_cast<std::size_t>(index)] > L'9') {
            throw std::invalid_argument{"invalid UTC timestamp"};
        }
    }
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    int consumed = 0;
    if (swscanf_s(
            value.c_str(), L"%4d-%2d-%2dT%2d:%2d:%2d.%3dZ%n",
            &year, &month, &day, &hour, &minute, &second, &millisecond, &consumed) != 7 ||
        consumed != static_cast<int>(value.size())) {
        throw std::invalid_argument{"invalid UTC timestamp"};
    }
    const auto date = std::chrono::year{year} /
        std::chrono::month{static_cast<unsigned>(month)} /
        std::chrono::day{static_cast<unsigned>(day)};
    if (!date.ok() || year < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59 || millisecond < 0 || millisecond > 999) {
        throw std::invalid_argument{"invalid UTC timestamp"};
    }
    return Clock::time_point{
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::sys_days{date}.time_since_epoch())} +
        std::chrono::hours{hour} + std::chrono::minutes{minute} +
        std::chrono::seconds{second} + std::chrono::milliseconds{millisecond};
}

std::wstring format_time(Clock::time_point value) {
    using namespace std::chrono;
    const auto day_point = floor<days>(value);
    const year_month_day date{day_point};
    const auto remainder = value - day_point;
    const hh_mm_ss time{remainder};
    wchar_t buffer[32]{};
    swprintf_s(
        buffer, 32, L"%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ",
        static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()), static_cast<long long>(time.hours().count()),
        static_cast<long long>(time.minutes().count()), static_cast<long long>(time.seconds().count()),
        static_cast<long long>(duration_cast<milliseconds>(time.subseconds()).count()));
    return buffer;
}

std::wstring named_string(const JsonObject& object, std::wstring_view name, std::wstring fallback = {}) {
    const winrt::hstring key{name};
    return object.HasKey(key) ? std::wstring{object.GetNamedString(key)} : std::move(fallback);
}

bool named_bool(const JsonObject& object, std::wstring_view name, bool fallback) {
    const winrt::hstring key{name};
    return object.HasKey(key) ? object.GetNamedBoolean(key) : fallback;
}

double named_number(const JsonObject& object, std::wstring_view name, double fallback) {
    const winrt::hstring key{name};
    return object.HasKey(key) ? object.GetNamedNumber(key) : fallback;
}

Priority priority_from(std::wstring_view value) {
    if (value == L"high") return Priority::high;
    if (value == L"low") return Priority::low;
    return Priority::medium;
}

TaskStatus status_from(std::wstring_view value) {
    return value == L"done" ? TaskStatus::done : TaskStatus::todo;
}

Task decode_task(const JsonObject& object) {
    Task task;
    task.id = named_string(object, L"id");
    task.title = named_string(object, L"title");
    task.note = named_string(object, L"note");
    task.priority = priority_from(named_string(object, L"priority", L"medium"));
    task.status = status_from(named_string(object, L"status", L"todo"));
    task.due_at = parse_time(object, L"dueAt");
    task.remind = named_bool(object, L"remind", true);
    task.reminded_at = parse_time(object, L"remindedAt");
    task.order = named_number(object, L"order", 0.0);
    task.created_at = parse_time(object, L"createdAt").value_or(Clock::time_point{});
    task.updated_at = parse_time(object, L"updatedAt").value_or(Clock::time_point{});
    task.completed_at = parse_time(object, L"completedAt");
    if (object.HasKey(L"tags")) {
        for (const auto& value : object.GetNamedArray(L"tags")) {
            task.tags.push_back(std::wstring{value.GetString()});
        }
    }
    return task;
}

void decode_settings(const JsonObject& object, Settings& settings) {
    const auto theme = named_string(object, L"theme", L"system");
    settings.theme = theme == L"dark" ? Theme::dark : theme == L"light" ? Theme::light : Theme::system;
    const auto filter = named_string(object, L"defaultFilter", L"today");
    settings.default_filter = filter == L"week" ? ViewKind::week : filter == L"all" ? ViewKind::all :
        filter == L"done" ? ViewKind::done : ViewKind::today;
    settings.hotkey = named_string(object, L"hotkey", settings.hotkey);
    settings.selectable_hotkey = named_string(
        object, L"hotkeySelectable",
        named_string(object, L"selectableHotkey", settings.selectable_hotkey));
    settings.week_starts_on = static_cast<int>(named_number(object, L"weekStartsOn", 1));
    settings.remind_advance_minutes = static_cast<int>(named_number(object, L"remindAdvanceMinutes", 0));
    settings.auto_start = named_bool(object, L"autoStart", false);
    settings.start_minimized = named_bool(object, L"startMinimized", false);
    settings.close_to_tray = named_bool(object, L"closeToTray", true);
    settings.window_mode = named_string(object, L"windowMode", L"normal") == L"floating" ?
        WindowMode::floating : WindowMode::normal;
    const auto layer = named_string(object, L"windowLayer", L"normal");
    settings.window_layer = layer == L"top" ? WindowLayer::top :
        layer == L"bottom" ? WindowLayer::bottom : WindowLayer::normal;
    settings.selectable = named_bool(object, L"selectable", true);
    settings.multi_select_enabled = named_bool(object, L"multiSelectEnabled", true);
    settings.rubber_band_select = named_bool(object, L"rubberBandSelect", true);
    settings.keep_selection_across_views = named_bool(object, L"keepSelectionAcrossViews", true);
    if (object.HasKey(L"floatingGeometry")) {
        const auto geometry = object.GetNamedObject(L"floatingGeometry");
        if (geometry.HasKey(L"x") && geometry.GetNamedValue(L"x").ValueType() !=
            winrt::Windows::Data::Json::JsonValueType::Null) {
            settings.floating_geometry.x = geometry.GetNamedNumber(L"x");
        }
        if (geometry.HasKey(L"y") && geometry.GetNamedValue(L"y").ValueType() !=
            winrt::Windows::Data::Json::JsonValueType::Null) {
            settings.floating_geometry.y = geometry.GetNamedNumber(L"y");
        }
        settings.floating_geometry.width = named_number(
            geometry, L"w", named_number(geometry, L"width", 360.0));
        settings.floating_geometry.height = named_number(
            geometry, L"h", named_number(geometry, L"height", 480.0));
    }
}

void put(JsonObject& object, std::wstring_view key, std::wstring_view value) {
    object.Insert(winrt::hstring{key}, JsonValue::CreateStringValue(winrt::hstring{value}));
}
void put(JsonObject& object, std::wstring_view key, const wchar_t* value) {
    put(object, key, std::wstring_view{value});
}
void put(JsonObject& object, std::wstring_view key, bool value) {
    object.Insert(winrt::hstring{key}, JsonValue::CreateBooleanValue(value));
}
void put(JsonObject& object, std::wstring_view key, double value) {
    object.Insert(winrt::hstring{key}, JsonValue::CreateNumberValue(value));
}
void put_time(JsonObject& object, std::wstring_view key, std::optional<Clock::time_point> value) {
    if (value.has_value()) put(object, key, format_time(*value));
    else object.Insert(winrt::hstring{key}, JsonValue::CreateNullValue());
}

JsonObject encode_task(const Task& task) {
    JsonObject object;
    put(object, L"id", task.id); put(object, L"title", task.title); put(object, L"note", task.note);
    put(object, L"priority", task.priority == Priority::high ? L"high" : task.priority == Priority::low ? L"low" : L"medium");
    put(object, L"status", task.status == TaskStatus::done ? L"done" : L"todo");
    put_time(object, L"dueAt", task.due_at); put(object, L"remind", task.remind);
    put_time(object, L"remindedAt", task.reminded_at);
    JsonArray tags; for (const auto& tag : task.tags) tags.Append(JsonValue::CreateStringValue(tag));
    object.Insert(L"tags", tags); put(object, L"order", task.order);
    put_time(object, L"createdAt", task.created_at); put_time(object, L"updatedAt", task.updated_at);
    put_time(object, L"completedAt", task.completed_at);
    return object;
}

JsonObject encode_settings(const Settings& settings) {
    JsonObject object;
    put(object, L"theme", settings.theme == Theme::dark ? L"dark" : settings.theme == Theme::light ? L"light" : L"system");
    const auto filter = settings.default_filter == ViewKind::week ? L"week" : settings.default_filter == ViewKind::all ? L"all" : settings.default_filter == ViewKind::done ? L"done" : L"today";
    put(object, L"defaultFilter", filter); put(object, L"hotkey", settings.hotkey);
    put(object, L"hotkeySelectable", settings.selectable_hotkey); put(object, L"weekStartsOn", static_cast<double>(settings.week_starts_on));
    put(object, L"remindAdvanceMinutes", static_cast<double>(settings.remind_advance_minutes));
    put(object, L"autoStart", settings.auto_start); put(object, L"startMinimized", settings.start_minimized);
    put(object, L"closeToTray", settings.close_to_tray);
    put(object, L"windowMode", settings.window_mode == WindowMode::floating ? L"floating" : L"normal");
    put(object, L"windowLayer", settings.window_layer == WindowLayer::top ? L"top" : settings.window_layer == WindowLayer::bottom ? L"bottom" : L"normal");
    put(object, L"selectable", settings.selectable);
    JsonObject geometry;
    if (settings.floating_geometry.x) put(geometry, L"x", *settings.floating_geometry.x); else geometry.Insert(L"x", JsonValue::CreateNullValue());
    if (settings.floating_geometry.y) put(geometry, L"y", *settings.floating_geometry.y); else geometry.Insert(L"y", JsonValue::CreateNullValue());
    put(geometry, L"w", settings.floating_geometry.width); put(geometry, L"h", settings.floating_geometry.height);
    object.Insert(L"floatingGeometry", geometry);
    put(object, L"multiSelectEnabled", settings.multi_select_enabled); put(object, L"rubberBandSelect", settings.rubber_band_select);
    put(object, L"keepSelectionAcrossViews", settings.keep_selection_across_views);
    return object;
}

}  // namespace

DecodeResult decode_state_utf8(std::span<const std::byte> source) {
    try {
        ensure_apartment();
        const auto root = JsonObject::Parse(strict_wide(source));
        AppState state;
        const auto schema = root.GetNamedNumber(L"schemaVersion");
        if (!std::isfinite(schema) || schema != 1.0) {
            return {std::nullopt, {{ValidationIssueCode::unsupported_schema, std::nullopt}},
                L"Unsupported schema"};
        }
        state.schema_version = 1;
        if (root.HasKey(L"tasks")) {
            for (const auto& value : root.GetNamedArray(L"tasks")) {
                state.tasks.push_back(decode_task(value.GetObject()));
            }
        }
        if (root.HasKey(L"settings")) decode_settings(root.GetNamedObject(L"settings"), state.settings);
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        auto validation = validate_state(std::move(state), now);
        if (!validation.supported) return {std::nullopt, std::move(validation.issues), L"Unsupported schema"};
        return {std::move(validation.state), std::move(validation.issues), {}};
    } catch (const std::exception& error) {
        const auto message = std::string{error.what()};
        return {std::nullopt, {}, std::wstring{message.begin(), message.end()}};
    } catch (const winrt::hresult_error& error) {
        return {std::nullopt, {}, std::wstring{error.message()}};
    }
}

std::vector<std::byte> encode_state_utf8(const AppState& state) {
    ensure_apartment();
    JsonObject root;
    put(root, L"schemaVersion", static_cast<double>(state.schema_version));
    JsonArray tasks; for (const auto& task : state.tasks) tasks.Append(encode_task(task));
    root.Insert(L"tasks", tasks); root.Insert(L"settings", encode_settings(state.settings));
    return strict_utf8(std::wstring_view{root.Stringify()});
}

}  // namespace desktop_todo
