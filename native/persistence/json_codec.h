#pragma once

#include "domain/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace desktop_todo {

struct DecodeResult {
    std::optional<AppState> state;
    std::vector<ValidationIssue> issues;
    std::wstring error;
};

[[nodiscard]] DecodeResult decode_state_utf8(std::span<const std::byte> source);
[[nodiscard]] std::vector<std::byte> encode_state_utf8(const AppState& state);

}  // namespace desktop_todo
