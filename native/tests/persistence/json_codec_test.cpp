#include "test_support.h"

#include "persistence/json_codec.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using desktop_todo::AppState;
using desktop_todo::Clock;
using desktop_todo::Priority;
using desktop_todo::Task;
using desktop_todo::Theme;
using desktop_todo::ViewKind;
using desktop_todo::WindowLayer;
using desktop_todo::WindowMode;

std::vector<std::byte> bytes(std::string_view text) {
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    return {first, first + text.size()};
}

Clock::time_point time_at(std::int64_t milliseconds) {
    return Clock::time_point{std::chrono::milliseconds{milliseconds}};
}

AppState representative_state() {
    AppState state;
    Task task;
    task.id = L"任务-😀";
    task.title = L"中文任务 😀";
    task.note = L"第一行\r\n第二行";
    task.priority = Priority::high;
    task.due_at = time_at(1'800'000'000'000);
    task.remind = true;
    task.reminded_at = time_at(1'799'999'000'000);
    task.tags = {L"工作", L"重要"};
    task.order = 2.5;
    task.created_at = time_at(1'700'000'000'000);
    task.updated_at = time_at(1'700'000'010'000);
    state.tasks = {task};
    state.settings.theme = Theme::dark;
    state.settings.default_filter = ViewKind::all;
    state.settings.window_mode = WindowMode::floating;
    state.settings.window_layer = WindowLayer::top;
    state.settings.close_to_tray = false;
    state.settings.remind_advance_minutes = 10;
    state.settings.floating_geometry.x = 20.0;
    state.settings.floating_geometry.y = 30.0;
    return state;
}

}  // namespace

TEST_CASE(json_codec_round_trip_preserves_schema_chinese_emoji_and_crlf) {
    const auto original = representative_state();

    const auto encoded = desktop_todo::encode_state_utf8(original);
    const std::string encoded_text{
        reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    EXPECT_TRUE(encoded_text.find("\"closeToTray\":false") != std::string::npos);
    const auto decoded = desktop_todo::decode_state_utf8(encoded);

    EXPECT_TRUE(decoded.state.has_value());
    EXPECT_EQ(decoded.state->schema_version, 1);
    EXPECT_EQ(decoded.state->tasks.size(), std::size_t{1});
    EXPECT_EQ(decoded.state->tasks[0].id, original.tasks[0].id);
    EXPECT_EQ(decoded.state->tasks[0].title, original.tasks[0].title);
    EXPECT_EQ(decoded.state->tasks[0].note, original.tasks[0].note);
    EXPECT_EQ(decoded.state->tasks[0].tags, original.tasks[0].tags);
    EXPECT_EQ(decoded.state->tasks[0].due_at, original.tasks[0].due_at);
    EXPECT_EQ(decoded.state->tasks[0].reminded_at, original.tasks[0].reminded_at);
    EXPECT_EQ(decoded.state->settings.theme, Theme::dark);
    EXPECT_EQ(decoded.state->settings.window_mode, WindowMode::floating);
    EXPECT_TRUE(!decoded.state->settings.close_to_tray);
}

TEST_CASE(json_codec_ignores_unknown_fields_and_loads_fixture) {
    const std::filesystem::path fixture =
        std::filesystem::path{DESKTOP_TODO_SOURCE_DIR} /
        L"native/tests/fixtures/schema-v1-export.json";
    std::ifstream input{fixture, std::ios::binary};
    const std::string json{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    auto with_unknown = json;
    const auto position = with_unknown.rfind('}');
    with_unknown.insert(position, R"(,"futureField":{"ignored":true})");

    const auto decoded = desktop_todo::decode_state_utf8(bytes(with_unknown));

    EXPECT_TRUE(decoded.state.has_value());
    EXPECT_EQ(decoded.state->tasks[0].id, L"fixture-1");
    EXPECT_EQ(decoded.state->tasks[0].title, L"中文任务 😀");
    EXPECT_EQ(decoded.state->settings.remind_advance_minutes, 10);
    EXPECT_EQ(decoded.state->settings.selectable_hotkey, L"Ctrl+Shift+K");
    EXPECT_EQ(decoded.state->settings.floating_geometry.width, 444.0);
    EXPECT_EQ(decoded.state->settings.floating_geometry.height, 555.0);
}

TEST_CASE(json_codec_rejects_invalid_utf8) {
    const std::vector<std::byte> invalid_utf8{
        std::byte{'{'}, std::byte{'"'}, std::byte{0xC3}, std::byte{0x28}, std::byte{'}'}};

    const auto invalid = desktop_todo::decode_state_utf8(invalid_utf8);
    EXPECT_TRUE(!invalid.state.has_value() && !invalid.error.empty());
}

TEST_CASE(json_codec_rejects_malformed_json) {
    const auto malformed = desktop_todo::decode_state_utf8(bytes("{broken"));
    EXPECT_TRUE(!malformed.state.has_value() && !malformed.error.empty());
}

TEST_CASE(json_codec_rejects_future_schema) {
    const auto future = desktop_todo::decode_state_utf8(
        bytes(R"({"schemaVersion":2,"tasks":[],"settings":{}})"));
    EXPECT_TRUE(!future.state.has_value() && !future.error.empty());
}

TEST_CASE(json_codec_drops_blank_title_task_with_issue) {
    const auto decoded = desktop_todo::decode_state_utf8(bytes(
        R"({"schemaVersion":1,"tasks":[{"id":"bad","title":"   ","createdAt":"2026-09-26T01:00:00.000Z","updatedAt":"2026-09-26T01:00:00.000Z"}],"settings":{}})"));

    EXPECT_TRUE(decoded.state.has_value());
    EXPECT_TRUE(decoded.state->tasks.empty());
    EXPECT_TRUE(!decoded.issues.empty());
}

TEST_CASE(json_codec_preserves_legacy_schema_v1_setting_names) {
    const auto decoded = desktop_todo::decode_state_utf8(bytes(
        R"({"schemaVersion":1,"tasks":[],"settings":{"hotkeySelectable":"Ctrl+Shift+K","floatingGeometry":{"x":1,"y":2,"w":444,"h":555}}})"));

    EXPECT_TRUE(decoded.state.has_value());
    EXPECT_EQ(decoded.state->settings.selectable_hotkey, L"Ctrl+Shift+K");
    EXPECT_TRUE(decoded.state->settings.close_to_tray);
    EXPECT_EQ(decoded.state->settings.floating_geometry.width, 444.0);
    EXPECT_EQ(decoded.state->settings.floating_geometry.height, 555.0);
    const auto encoded = desktop_todo::encode_state_utf8(*decoded.state);
    const std::string output{reinterpret_cast<const char*>(encoded.data()), encoded.size()};
    EXPECT_TRUE(output.find("hotkeySelectable") != std::string::npos);
    EXPECT_TRUE(output.find("\"w\":444") != std::string::npos);
}

TEST_CASE(json_codec_rejects_fractional_schema_and_signed_timestamp_fields) {
    const auto schema = desktop_todo::decode_state_utf8(
        bytes(R"({"schemaVersion":1.5,"tasks":[],"settings":{}})"));
    const auto timestamp = desktop_todo::decode_state_utf8(bytes(
        R"({"schemaVersion":1,"tasks":[{"id":"bad","title":"bad","createdAt":"2026-09-26T-1:00:00.000Z","updatedAt":"2026-09-26T01:00:00.000Z"}],"settings":{}})"));

    EXPECT_TRUE(!schema.state.has_value());
    EXPECT_TRUE(!timestamp.state.has_value());
}
