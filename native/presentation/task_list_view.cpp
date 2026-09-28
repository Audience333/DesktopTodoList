#include "presentation/task_list_view.h"

#include <algorithm>
#include <cmath>

namespace desktop_todo {
namespace {

bool contains(const RectF& rectangle, PointF point) {
    return point.x >= rectangle.x && point.x < rectangle.right() &&
        point.y >= rectangle.y && point.y < rectangle.bottom();
}

}  // namespace

VisibleRange calculate_visible_range(
    float scroll_y,
    float viewport_height,
    float row_height,
    std::size_t row_count,
    std::size_t overscan) {
    if (row_count == 0 || viewport_height <= 0 || row_height <= 0 ||
        !std::isfinite(scroll_y) || !std::isfinite(viewport_height) ||
        !std::isfinite(row_height)) {
        return {};
    }
    const auto content_height = static_cast<float>(row_count) * row_height;
    const auto maximum_scroll = std::max(0.0F, content_height - viewport_height);
    const auto clamped_scroll = std::clamp(scroll_y, 0.0F, maximum_scroll);
    const auto visible_first = static_cast<std::size_t>(std::floor(clamped_scroll / row_height));
    const auto visible_last = static_cast<std::size_t>(
        std::ceil((clamped_scroll + viewport_height) / row_height));
    const auto first = visible_first > overscan ? visible_first - overscan : 0;
    const auto padded_last = visible_last > row_count - std::min(overscan, row_count)
        ? row_count
        : visible_last + overscan;
    return {std::min(first, row_count), std::min(padded_last, row_count)};
}

std::size_t preserve_scroll_anchor(
    const std::vector<std::wstring>& previous_ids,
    const std::vector<std::wstring>& current_ids,
    std::size_t previous_index) {
    if (current_ids.empty()) return 0;
    if (previous_ids.empty()) return std::min(previous_index, current_ids.size() - 1);

    const auto anchor_index = std::min(previous_index, previous_ids.size() - 1);
    const auto find_current = [&current_ids](const std::wstring& id) {
        return std::find(current_ids.begin(), current_ids.end(), id);
    };
    if (const auto exact = find_current(previous_ids[anchor_index]); exact != current_ids.end()) {
        return static_cast<std::size_t>(exact - current_ids.begin());
    }
    for (std::size_t distance = 1; distance < previous_ids.size(); ++distance) {
        if (anchor_index + distance < previous_ids.size()) {
            const auto next = find_current(previous_ids[anchor_index + distance]);
            if (next != current_ids.end()) {
                return static_cast<std::size_t>(next - current_ids.begin());
            }
        }
        if (distance <= anchor_index) {
            const auto prior = find_current(previous_ids[anchor_index - distance]);
            if (prior != current_ids.end()) {
                return static_cast<std::size_t>(prior - current_ids.begin());
            }
        }
    }
    return std::min(anchor_index, current_ids.size() - 1);
}

RowHitArea hit_test_row(PointF point, const RowHitZones& zones) {
    if (!contains(zones.row, point)) return RowHitArea::none;
    if (contains(zones.checkbox, point)) return RowHitArea::checkbox;
    if (contains(zones.delete_button, point)) return RowHitArea::delete_button;
    if (contains(zones.drag_handle, point)) return RowHitArea::drag_handle;
    if (contains(zones.title, point)) return RowHitArea::title;
    return RowHitArea::row;
}

}  // namespace desktop_todo
