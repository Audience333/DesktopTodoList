#pragma once

#include <optional>

namespace desktop_todo {

struct SizeF {
    float width = 0;
    float height = 0;
    bool operator==(const SizeF&) const = default;
};

struct RectF {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    [[nodiscard]] float right() const noexcept { return x + width; }
    [[nodiscard]] float bottom() const noexcept { return y + height; }
    bool operator==(const RectF&) const = default;
};

struct RectI {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool operator==(const RectI&) const = default;
};

struct MonitorInfo {
    RectI work_area;
    float dpi = 96.0F;
};

enum class LayoutMode { compact, expanded };

struct LayoutResult {
    float scale = 1.0F;
    SizeF logical_client;
    RectF title;
    RectF quick_add;
    RectF tabs;
    RectF task_list;
    RectF footer;
    std::optional<RectF> details;
};

[[nodiscard]] SizeF default_widget_size(float dpi);
[[nodiscard]] float minimum_widget_width(float dpi);
[[nodiscard]] LayoutResult calculate_layout(SizeF client, float dpi, LayoutMode mode);
[[nodiscard]] RectI clamp_to_work_area(RectI saved, MonitorInfo monitor);

}  // namespace desktop_todo
