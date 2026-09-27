#pragma once

#include "domain/types.h"

namespace desktop_todo {

[[nodiscard]] TaskValidation validate_task(
    Task task,
    double fallback_order,
    Clock::time_point now);

[[nodiscard]] StateValidation validate_state(
    AppState state,
    Clock::time_point now);

}  // namespace desktop_todo
