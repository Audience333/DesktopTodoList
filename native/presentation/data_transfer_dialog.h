#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "persistence/import_export.h"
#include "persistence/state_repository.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace desktop_todo {

class AppService;

struct TransferResult {
    bool accepted = false;
    std::size_t changed = 0;
    std::wstring error;
};

class DataTransferDialog {
public:
    [[nodiscard]] static bool accepts_json_path(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<std::vector<std::byte>> read_json_file(
        const std::filesystem::path& path, std::wstring& error);
    [[nodiscard]] static TransferResult import_bytes(
        AppService& service, std::span<const std::byte> source,
        std::optional<ImportMode> mode, bool confirmed);
    [[nodiscard]] static ExportResult export_to(
        AppService& service, const std::filesystem::path& destination);
    [[nodiscard]] static bool reset_with_confirmation(
        AppService& service, bool first_confirmation, bool second_confirmation);
    [[nodiscard]] static std::optional<std::filesystem::path> choose_import_file(HWND owner);
    [[nodiscard]] static std::optional<std::filesystem::path> choose_export_file(HWND owner);
};

}  // namespace desktop_todo
