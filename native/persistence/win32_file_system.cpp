#include "persistence/file_system.h"

#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <limits>

namespace desktop_todo {
namespace {

std::wstring error_message(DWORD code) {
    wchar_t* buffer = nullptr;
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length == 0 ? L"Windows file operation failed" :
        std::wstring{buffer, static_cast<std::size_t>(length)};
    if (buffer != nullptr) LocalFree(buffer);
    return result;
}

FileOperationResult failure() {
    return {false, error_message(GetLastError())};
}

class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

class FindHandle {
public:
    explicit FindHandle(HANDLE value) : value_(value) {}
    ~FindHandle() { if (value_ != INVALID_HANDLE_VALUE) FindClose(value_); }
    FindHandle(const FindHandle&) = delete;
    FindHandle& operator=(const FindHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

}  // namespace

bool Win32FileSystem::exists(const std::filesystem::path& path) const {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

FileReadResult Win32FileSystem::read(const std::filesystem::path& path) const {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file.valid()) return {{}, error_message(GetLastError())};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        size.QuadPart > static_cast<LONGLONG>((std::numeric_limits<DWORD>::max)())) {
        return {{}, L"State file is too large"};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read_count = 0;
    if (!bytes.empty() && !ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
        &read_count, nullptr)) return {{}, error_message(GetLastError())};
    if (read_count != bytes.size()) return {{}, L"State file read was incomplete"};
    return {std::move(bytes), {}};
}

FileOperationResult Win32FileSystem::write(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes) {
    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error) return {false, L"Cannot create data directory"};
    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file.valid()) return failure();
    DWORD written = 0;
    if (!bytes.empty() && !WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()),
        &written, nullptr)) return failure();
    return written == bytes.size() ? FileOperationResult{} :
        FileOperationResult{false, L"State file write was incomplete"};
}

FileOperationResult Win32FileSystem::flush(const std::filesystem::path& path) {
    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file.valid()) return failure();
    return FlushFileBuffers(file.get()) ? FileOperationResult{} : failure();
}

FileOperationResult Win32FileSystem::replace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    if (exists(destination)) {
        if (ReplaceFileW(destination.c_str(), source.c_str(), nullptr,
            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) return {};
        return failure();
    }
    return MoveFileExW(source.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? FileOperationResult{} : failure();
}

FileOperationResult Win32FileSystem::move(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    return MoveFileExW(source.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? FileOperationResult{} : failure();
}

FileOperationResult Win32FileSystem::remove(const std::filesystem::path& path) {
    return DeleteFileW(path.c_str()) ? FileOperationResult{} : failure();
}

std::vector<std::filesystem::path> Win32FileSystem::list(
    const std::filesystem::path& directory) const {
    std::vector<std::filesystem::path> result;
    WIN32_FIND_DATAW data{};
    FindHandle search{FindFirstFileW((directory / L"*").c_str(), &data)};
    if (!search.valid()) return result;
    do {
        const std::wstring_view name{data.cFileName};
        if (name != L"." && name != L".." &&
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            result.push_back(directory / name);
        }
    } while (FindNextFileW(search.get(), &data));
    return result;
}

}  // namespace desktop_todo
