#pragma once

#include "domain/types.h"
#include "persistence/file_system.h"

#include <filesystem>
#include <functional>
#include <string>

namespace desktop_todo {

struct LocalDate {
    int year;
    int month;
    int day;
};

enum class LoadStatus { fresh, ok, restored, reset };

struct LoadResult {
    AppState state;
    LoadStatus status = LoadStatus::fresh;
    std::wstring error;
};

struct SaveResult {
    bool ok = true;
    std::wstring error;
};

using BackupResult = SaveResult;

struct ExportResult {
    bool ok = true;
    std::filesystem::path path;
    std::wstring error;
};

class StateRepository {
public:
    StateRepository(
        IFileSystem& files,
        std::filesystem::path directory,
        std::function<std::wstring()> timestamp);

    [[nodiscard]] LoadResult load();
    [[nodiscard]] SaveResult save(const AppState& state);
    [[nodiscard]] BackupResult ensure_daily_backup(const AppState& state, LocalDate today);
    [[nodiscard]] ExportResult preserve_corrupt_source();

private:
    [[nodiscard]] SaveResult save_to(
        const std::filesystem::path& destination,
        const AppState& state);

    IFileSystem& files_;
    std::filesystem::path directory_;
    std::function<std::wstring()> timestamp_;
};

}  // namespace desktop_todo
