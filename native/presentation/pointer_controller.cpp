#include "presentation/pointer_controller.h"

#include <algorithm>
#include <cmath>

namespace desktop_todo {
namespace {

constexpr float drag_threshold = 4.0F;

bool intersects(const RectF& left, const RectF& right) {
    return left.x < right.right() && left.right() > right.x &&
        left.y < right.bottom() && left.bottom() > right.y;
}

}  // namespace

PointerController::PointerController(SelectionModel& selection) : selection_(selection) {}

void PointerController::set_features(bool multi_select_enabled, bool rubber_band_enabled) noexcept {
    if (rubber_band_enabled_ && !rubber_band_enabled &&
        (gesture_ == Gesture::blank_pending || gesture_ == Gesture::rubber_band)) {
        cancel();
    }
    multi_select_enabled_ = multi_select_enabled;
    rubber_band_enabled_ = rubber_band_enabled;
}

PointerAction PointerController::press(
    RowHitArea area,
    std::wstring_view task_id,
    PointF point,
    const std::vector<std::wstring>& visible_ids,
    bool ctrl,
    bool shift) {
    cancel();
    const std::wstring id{task_id};
    switch (area) {
    case RowHitArea::checkbox:
        return {PointerActionKind::toggle_completion, id};
    case RowHitArea::title:
        return {PointerActionKind::begin_inline_edit, id};
    case RowHitArea::delete_button:
        return {PointerActionKind::delete_task, id};
    case RowHitArea::drag_handle:
        gesture_ = Gesture::dragging;
        start_ = point;
        current_ = point;
        source_id_ = id;
        capturing_ = true;
        return {PointerActionKind::begin_drag, id};
    case RowHitArea::row:
        if (multi_select_enabled_ && shift) selection_.select_range(id, visible_ids, ctrl);
        else if (multi_select_enabled_ && ctrl) selection_.toggle(id);
        else selection_.select_one(id);
        return {PointerActionKind::selection_changed, id};
    case RowHitArea::none:
        if (!rubber_band_enabled_) return {};
        gesture_ = Gesture::blank_pending;
        start_ = point;
        current_ = point;
        append_selection_ = multi_select_enabled_ && ctrl;
        initial_selection_ = selection_.snapshot().selected_ids;
        return {};
    }
    return {};
}

PointerAction PointerController::move(
    PointF point,
    const std::vector<PointerRowBounds>& visible_rows) {
    if (gesture_ == Gesture::blank_pending &&
        (std::abs(point.x - start_.x) > drag_threshold ||
            std::abs(point.y - start_.y) > drag_threshold)) {
        gesture_ = Gesture::rubber_band;
        capturing_ = true;
    }
    if (gesture_ != Gesture::rubber_band) return {};
    current_ = point;
    update_rubber_band(point, visible_rows);
    return {PointerActionKind::rubber_band};
}

PointerAction PointerController::release(
    PointF point,
    const std::vector<PointerRowBounds>& visible_rows) {
    if (gesture_ == Gesture::blank_pending) {
        cancel();
        return {};
    }
    if (gesture_ == Gesture::rubber_band) {
        current_ = point;
        update_rubber_band(point, visible_rows);
        cancel();
        return {PointerActionKind::end_rubber_band};
    }
    if (gesture_ == Gesture::dragging) {
        PointerAction result;
        const auto target = std::find_if(visible_rows.begin(), visible_rows.end(),
            [point, this](const PointerRowBounds& row) {
                return row.id != source_id_ && point.x >= row.bounds.x &&
                    point.x < row.bounds.right() && point.y >= row.bounds.y &&
                    point.y < row.bounds.bottom();
            });
        if (target != visible_rows.end()) {
            result.kind = PointerActionKind::reorder;
            result.task_id = source_id_;
            result.target_id = target->id;
            result.position = point.y >= target->bounds.y + target->bounds.height / 2
                ? DropPosition::after : DropPosition::before;
        }
        cancel();
        return result;
    }
    return {};
}

void PointerController::cancel() noexcept {
    gesture_ = Gesture::idle;
    source_id_.clear();
    initial_selection_.clear();
    capturing_ = false;
}

bool PointerController::capturing() const noexcept { return capturing_; }

bool PointerController::active() const noexcept { return gesture_ != Gesture::idle; }

std::optional<RectF> PointerController::rubber_band() const noexcept {
    if (gesture_ != Gesture::rubber_band) return std::nullopt;
    const auto left = std::min(start_.x, current_.x);
    const auto top = std::min(start_.y, current_.y);
    return RectF{left, top, std::abs(current_.x - start_.x), std::abs(current_.y - start_.y)};
}

void PointerController::update_rubber_band(
    PointF point,
    const std::vector<PointerRowBounds>& visible_rows) {
    const auto left = std::min(start_.x, point.x);
    const auto top = std::min(start_.y, point.y);
    const RectF band{left, top, std::abs(point.x - start_.x), std::abs(point.y - start_.y)};
    std::vector<std::wstring> selected;
    if (append_selection_) selected = initial_selection_;
    for (const auto& row : visible_rows) {
        if (intersects(band, row.bounds)) selected.push_back(row.id);
    }
    selection_.select_ids(selected, false);
}

}  // namespace desktop_todo
