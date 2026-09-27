#pragma once

#include "persistence/file_system.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace desktop_todo::test_support {

class FakeFileSystem final : public IFileSystem {
public:
    bool exists(const std::filesystem::path& path) const override {
        return files.contains(path.generic_wstring());
    }

    FileReadResult read(const std::filesystem::path& path) const override {
        const auto found = files.find(path.generic_wstring());
        return found == files.end()
            ? FileReadResult{{}, L"not found"}
            : FileReadResult{found->second, {}};
    }

    FileOperationResult write(
        const std::filesystem::path& path,
        std::span<const std::byte> bytes) override {
        if (!record(L"write:" + path.filename().wstring())) return {false, L"injected"};
        files[path.generic_wstring()] = {bytes.begin(), bytes.end()};
        return {};
    }

    FileOperationResult flush(const std::filesystem::path& path) override {
        return record(L"flush:" + path.filename().wstring())
            ? FileOperationResult{}
            : FileOperationResult{false, L"injected"};
    }

    FileOperationResult replace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) override {
        if (!record(L"replace:" + destination.filename().wstring())) return {false, L"injected"};
        const auto found = files.find(source.generic_wstring());
        if (found == files.end()) return {false, L"source missing"};
        files[destination.generic_wstring()] = found->second;
        files.erase(found);
        return {};
    }

    FileOperationResult move(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) override {
        if (!record(L"move:" + destination.filename().wstring())) return {false, L"injected"};
        const auto found = files.find(source.generic_wstring());
        if (found == files.end()) return {false, L"source missing"};
        files[destination.generic_wstring()] = found->second;
        files.erase(found);
        return {};
    }

    FileOperationResult remove(const std::filesystem::path& path) override {
        if (!record(L"remove:" + path.filename().wstring())) return {false, L"injected"};
        files.erase(path.generic_wstring());
        return {};
    }

    std::vector<std::filesystem::path> list(const std::filesystem::path& directory) const override {
        std::vector<std::filesystem::path> result;
        for (const auto& [name, ignored] : files) {
            const std::filesystem::path path{name};
            if (path.parent_path() == directory) result.push_back(path);
        }
        return result;
    }

    std::map<std::wstring, std::vector<std::byte>> files;
    std::vector<std::wstring> operations;
    std::optional<std::size_t> fail_operation;

private:
    bool record(std::wstring operation) {
        operations.push_back(std::move(operation));
        return !fail_operation.has_value() || operations.size() != *fail_operation;
    }
};

}  // namespace desktop_todo::test_support
