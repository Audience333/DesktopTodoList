#include "presentation/theme.h"

namespace desktop_todo {

ThemePalette resolve_theme(Theme requested, SystemTheme system) {
    if (system.high_contrast) {
        return {0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF,
            0xFFFFFF00, 0xFFFFFFFF, true, system.reduced_motion};
    }
    const auto dark = requested == Theme::dark || (requested == Theme::system && system.dark);
    if (dark) {
        return {0xFF171A21, 0xFF222733, 0xFFF4F7FB, 0xFFAAB2C0,
            0xFF7AA2FF, 0xFF343B49, false, system.reduced_motion};
    }
    return {0xFFF5F7FA, 0xFFFFFFFF, 0xFF222831, 0xFF667085,
        0xFF356AE6, 0xFFD8DEE8, false, system.reduced_motion};
}

}  // namespace desktop_todo
