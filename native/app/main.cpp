#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "application/app_service.h"
#include "persistence/file_system.h"
#include "persistence/json_codec.h"
#include "persistence/state_repository.h"

#include <chrono>
#include <filesystem>
#include <string_view>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        wchar_t root[MAX_PATH]{};
        const auto length = GetTempPathW(MAX_PATH, root);
        if (length == 0 || length >= MAX_PATH) return;
        path = std::filesystem::path{root} /
            (L"DesktopTodoList-core-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

int verify_core(const std::filesystem::path& fixture) {
    desktop_todo::Win32FileSystem files;
    const auto source = files.read(fixture);
    if (!source.ok()) return 10;
    const auto decoded = desktop_todo::decode_state_utf8(source.bytes);
    if (!decoded.state.has_value()) return 11;
    TemporaryDirectory temporary;
    if (temporary.path.empty()) return 12;
    desktop_todo::StateRepository repository{
        files, temporary.path, [] { return L"verification"; }};
    const auto now = [] {
        return std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
    };
    desktop_todo::AppService service{
        repository, now, [] { return L"verification-id"; },
        [] { return desktop_todo::LocalDate{2000, 1, 1}; }};
    if (!service.start()) return 13;
    auto prepared = service.prepare_import(source.bytes, desktop_todo::ImportMode::replace);
    if (!service.accept_import(std::move(prepared))) return 14;
    desktop_todo::QuerySpec query;
    query.include_completed = true;
    static_cast<void>(service.query(query));
    if (!service.flush()) return 15;
    desktop_todo::StateRepository reloader{
        files, temporary.path, [] { return L"verification-reload"; }};
    const auto loaded = reloader.load();
    const auto reloaded_json = desktop_todo::encode_state_utf8(loaded.state);
    const auto service_json = desktop_todo::encode_state_utf8(service.snapshot());
    return reloaded_json == service_json ? 0 : 16;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) return 2;
    const bool verify = count == 3 && std::wstring_view{arguments[1]} == L"--verify-core";
    const auto result = verify ? verify_core(arguments[2]) : 0;
    LocalFree(arguments);
    return result;
}
