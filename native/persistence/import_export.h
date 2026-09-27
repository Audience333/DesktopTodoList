#pragma once

#include "persistence/json_codec.h"

namespace desktop_todo {

enum class ImportMode { merge, replace };

struct ImportResult {
    std::optional<AppState> candidate;
    std::vector<ValidationIssue> issues;
    std::wstring error;
    std::size_t added = 0;
};

[[nodiscard]] ImportResult prepare_import(
    const AppState& current,
    std::span<const std::byte> source,
    ImportMode mode);

}  // namespace desktop_todo
