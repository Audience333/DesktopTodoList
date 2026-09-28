#pragma once

#include "domain/types.h"

#include <cstdint>

namespace desktop_todo {

struct SystemTheme {
    bool dark = false;
    bool high_contrast = false;
    bool reduced_motion = false;
};

struct ThemePalette {
    std::uint32_t background = 0;
    std::uint32_t surface = 0;
    std::uint32_t foreground = 0;
    std::uint32_t muted = 0;
    std::uint32_t accent = 0;
    std::uint32_t border = 0;
    bool high_contrast = false;
    bool reduced_motion = false;
};

[[nodiscard]] ThemePalette resolve_theme(Theme requested, SystemTheme system);

}  // namespace desktop_todo
