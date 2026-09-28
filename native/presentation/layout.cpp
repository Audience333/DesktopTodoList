#include "presentation/layout.h"

#include <algorithm>

namespace desktop_todo {
namespace {

float dpi_scale(float dpi) {
    return std::max(dpi, 96.0F) / 96.0F;
}

}  // namespace

SizeF default_widget_size(float dpi) {
    const auto scale = dpi_scale(dpi);
    return {360.0F * scale, 480.0F * scale};
}

float minimum_widget_width(float dpi) {
    return 320.0F * dpi_scale(dpi);
}

LayoutResult calculate_layout(SizeF client, float dpi, LayoutMode mode) {
    const auto scale = dpi_scale(dpi);
    const SizeF logical{
        std::max(client.width / scale, 320.0F),
        std::max(client.height / scale, 320.0F)};
    constexpr float margin = 16.0F;
    constexpr float gap = 8.0F;
    LayoutResult result;
    result.scale = scale;
    result.logical_client = logical;
    result.title = {0, 0, logical.width, 56};
    result.quick_add = {margin, result.title.bottom() + gap, logical.width - margin * 2, 44};
    result.tabs = {margin, result.quick_add.bottom() + gap, logical.width - margin * 2, 36};
    result.footer = {margin, logical.height - 44, logical.width - margin * 2, 28};
    const auto list_top = result.tabs.bottom() + gap;
    const auto list_height = std::max(0.0F, result.footer.y - gap - list_top);
    if (mode == LayoutMode::expanded) {
        const auto details_width = std::max(120.0F, (logical.width - margin * 2 - gap) * 0.42F);
        result.task_list = {margin, list_top,
            logical.width - margin * 2 - gap - details_width, list_height};
        result.details = RectF{
            result.task_list.right() + gap, list_top, details_width, list_height};
    } else {
        result.task_list = {margin, list_top, logical.width - margin * 2, list_height};
    }
    return result;
}

RectI clamp_to_work_area(RectI saved, MonitorInfo monitor) {
    auto result = saved;
    result.width = std::clamp(result.width, 1, monitor.work_area.width);
    result.height = std::clamp(result.height, 1, monitor.work_area.height);
    result.x = std::clamp(result.x, monitor.work_area.x,
        monitor.work_area.x + monitor.work_area.width - result.width);
    result.y = std::clamp(result.y, monitor.work_area.y,
        monitor.work_area.y + monitor.work_area.height - result.height);
    return result;
}

}  // namespace desktop_todo
