#include "presentation/data_transfer_dialog.h"

#include "application/app_service.h"

#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <iterator>

namespace desktop_todo {
namespace {

constexpr std::size_t kMaximumImportBytes = 16 * 1024 * 1024;

std::optional<std::filesystem::path> choose_file(HWND owner, bool save) {
    std::array<wchar_t, 32768> path{};
    const wchar_t filter[] = L"JSON 文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_READONLY);
    const auto accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) return std::nullopt;
    return std::filesystem::path{path.data()};
}

}  // namespace

bool DataTransferDialog::accepts_json_path(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".json";
}

std::optional<std::vector<std::byte>> DataTransferDialog::read_json_file(
    const std::filesystem::path& path, std::wstring& error) {
    if (!accepts_json_path(path)) {
        error = L"只能导入 .json 文件。";
        return std::nullopt;
    }
    std::error_code status_error;
    const auto size = std::filesystem::file_size(path, status_error);
    if (status_error || size > kMaximumImportBytes) {
        error = status_error ? L"无法读取所选文件。" : L"JSON 文件超过 16 MB 限制。";
        return std::nullopt;
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        error = L"无法打开所选文件。";
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            error = L"文件读取不完整。";
            return std::nullopt;
        }
    }
    return bytes;
}

TransferResult DataTransferDialog::import_bytes(
    AppService& service, std::span<const std::byte> source,
    std::optional<ImportMode> mode, bool confirmed) {
    if (!mode.has_value() || !confirmed) return {};
    auto result = service.prepare_import(source, *mode);
    if (!result.candidate.has_value()) return {false, 0, std::move(result.error)};
    const auto changed = result.added;
    if (!service.accept_import(std::move(result)))
        return {false, 0, L"导入未完成；当前数据保持不变。"};
    return {true, changed, {}};
}

ExportResult DataTransferDialog::export_to(
    AppService& service, const std::filesystem::path& destination) {
    return service.export_to(destination);
}

bool DataTransferDialog::reset_with_confirmation(
    AppService& service, bool first_confirmation, bool second_confirmation) {
    return first_confirmation && second_confirmation && service.reset_to_defaults(true);
}

std::optional<std::filesystem::path> DataTransferDialog::choose_import_file(HWND owner) {
    return choose_file(owner, false);
}

std::optional<std::filesystem::path> DataTransferDialog::choose_export_file(HWND owner) {
    return choose_file(owner, true);
}

}  // namespace desktop_todo
