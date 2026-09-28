#include "persistence/state_repository.h"

#include "persistence/json_codec.h"

#include <algorithm>
#include <cstdio>

namespace desktop_todo {
namespace {

std::wstring date_name(LocalDate date) {
    wchar_t value[32]{};
    swprintf_s(value, 32, L"%04d-%02d-%02d.json", date.year, date.month, date.day);
    return value;
}

bool is_backup(const std::filesystem::path& path) {
    const auto name = path.filename().wstring();
    return name.size() == 15 && name[4] == L'-' && name[7] == L'-' &&
        name.ends_with(L".json");
}

}  // namespace

StateRepository::StateRepository(
    IFileSystem& files,
    std::filesystem::path directory,
    std::function<std::wstring()> timestamp)
    : files_(files), directory_(std::move(directory)), timestamp_(std::move(timestamp)) {}

LoadResult StateRepository::load() {
    const auto source = directory_ / L"state.json";
    const auto source_exists = files_.exists(source);
    std::vector<ValidationIssue> source_issues;
    std::filesystem::path preserved_source;
    if (source_exists) {
        const auto bytes = files_.read(source);
        if (bytes.ok()) {
            auto decoded = decode_state_utf8(bytes.bytes);
            if (decoded.state.has_value()) {
                const auto status = decoded.issues.empty() ? LoadStatus::ok : LoadStatus::repaired;
                return {std::move(*decoded.state), status, {}, std::move(decoded.issues), {}};
            }
            source_issues = std::move(decoded.issues);
        }

        const auto preserved = preserve_corrupt_source();
        if (!preserved.ok) {
            return {{}, LoadStatus::reset, preserved.error, std::move(source_issues), {}, false};
        }
        preserved_source = preserved.path;
    }
    auto backups = files_.list(directory_ / L"backups");
    std::erase_if(backups, [](const auto& path) { return !is_backup(path); });
    std::sort(backups.rbegin(), backups.rend());
    for (const auto& backup : backups) {
        const auto backup_bytes = files_.read(backup);
        if (!backup_bytes.ok()) continue;
        auto decoded = decode_state_utf8(backup_bytes.bytes);
        if (decoded.state.has_value()) {
            auto restored = std::move(*decoded.state);
            const auto saved = save(restored);
            return {std::move(restored), LoadStatus::restored,
                saved.ok ? std::wstring{} : saved.error,
                std::move(decoded.issues), std::move(preserved_source)};
        }
    }
    if (!source_exists && backups.empty()) return {};
    return {{}, LoadStatus::reset, L"No valid state or backup",
        std::move(source_issues), std::move(preserved_source)};
}

SaveResult StateRepository::save_to(
    const std::filesystem::path& destination,
    const AppState& state) {
    auto temporary = destination;
    temporary += L".tmp";
    const auto bytes = encode_state_utf8(state);
    const auto written = files_.write(temporary, bytes);
    if (!written.ok) return {false, written.error};
    const auto flushed = files_.flush(temporary);
    if (!flushed.ok) return {false, flushed.error};
    const auto replaced = files_.replace(temporary, destination);
    return {replaced.ok, replaced.error};
}

SaveResult StateRepository::save(const AppState& state) {
    return save_to(directory_ / L"state.json", state);
}

BackupResult StateRepository::ensure_daily_backup(const AppState& state, LocalDate today) {
    const auto backup_directory = directory_ / L"backups";
    const auto destination = backup_directory / date_name(today);
    if (files_.exists(destination)) return {};
    const auto saved = save_to(destination, state);
    if (!saved.ok) return saved;

    auto backups = files_.list(backup_directory);
    std::erase_if(backups, [](const auto& path) { return !is_backup(path); });
    std::sort(backups.begin(), backups.end());
    while (backups.size() > 7) {
        const auto removed = files_.remove(backups.front());
        if (!removed.ok) return {false, removed.error};
        backups.erase(backups.begin());
    }
    return {};
}

BackupResult StateRepository::create_reset_backup(const AppState& state) {
    return save_to(
        directory_ / L"backups" / (L"pre-reset-" + timestamp_() + L".json"),
        state);
}

BackupResult StateRepository::create_import_backup(const AppState& state) {
    return save_to(
        directory_ / L"backups" / (L"pre-import-" + timestamp_() + L".json"),
        state);
}

ExportResult StateRepository::preserve_corrupt_source() {
    const auto source = directory_ / L"state.json";
    if (!files_.exists(source)) return {};
    const auto destination = directory_ / (L"state.corrupt-" + timestamp_() + L".json");
    const auto moved = files_.move(source, destination);
    return {moved.ok, moved.ok ? destination : std::filesystem::path{}, moved.error};
}

ExportResult StateRepository::export_to(
    const std::filesystem::path& destination,
    const AppState& state) {
    const auto saved = save_to(destination, state);
    return {saved.ok, saved.ok ? destination : std::filesystem::path{}, saved.error};
}

}  // namespace desktop_todo
