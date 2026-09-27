#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace desktop_todo {

struct FileOperationResult {
    bool ok = true;
    std::wstring error;
};

struct FileReadResult {
    std::vector<std::byte> bytes;
    std::wstring error;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    [[nodiscard]] virtual bool exists(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual FileReadResult read(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual FileOperationResult write(
        const std::filesystem::path& path,
        std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual FileOperationResult flush(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual FileOperationResult replace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) = 0;
    [[nodiscard]] virtual FileOperationResult move(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) = 0;
    [[nodiscard]] virtual FileOperationResult remove(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual std::vector<std::filesystem::path> list(
        const std::filesystem::path& directory) const = 0;
};

class Win32FileSystem final : public IFileSystem {
public:
    [[nodiscard]] bool exists(const std::filesystem::path& path) const override;
    [[nodiscard]] FileReadResult read(const std::filesystem::path& path) const override;
    [[nodiscard]] FileOperationResult write(
        const std::filesystem::path& path,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] FileOperationResult flush(const std::filesystem::path& path) override;
    [[nodiscard]] FileOperationResult replace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) override;
    [[nodiscard]] FileOperationResult move(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) override;
    [[nodiscard]] FileOperationResult remove(const std::filesystem::path& path) override;
    [[nodiscard]] std::vector<std::filesystem::path> list(
        const std::filesystem::path& directory) const override;
};

}  // namespace desktop_todo
