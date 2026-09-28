#pragma once

#include "presentation/layout.h"

#include <cstddef>
#include <string>
#include <vector>

namespace desktop_todo {

struct VisibleRange {
    std::size_t first = 0;
    std::size_t last = 0;
    bool operator==(const VisibleRange&) const = default;
};

[[nodiscard]] VisibleRange calculate_visible_range(
    float scroll_y,
    float viewport_height,
    float row_height,
    std::size_t row_count,
    std::size_t overscan);

[[nodiscard]] std::size_t preserve_scroll_anchor(
    const std::vector<std::wstring>& previous_ids,
    const std::vector<std::wstring>& current_ids,
    std::size_t previous_index);

struct PointF {
    float x = 0;
    float y = 0;
};

enum class RowHitArea { none, checkbox, title, row, delete_button, drag_handle };

struct RowHitZones {
    RectF row;
    RectF checkbox;
    RectF title;
    RectF delete_button;
    RectF drag_handle;
};

[[nodiscard]] RowHitArea hit_test_row(PointF point, const RowHitZones& zones);

}  // namespace desktop_todo
