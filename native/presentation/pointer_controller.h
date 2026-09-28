#pragma once

#include "domain/commands.h"
#include "domain/selection_model.h"
#include "presentation/task_list_view.h"

#include <string>
#include <optional>
#include <vector>

namespace desktop_todo {

struct PointerRowBounds {
    std::wstring id;
    RectF bounds;
};

enum class PointerActionKind {
    none,
    selection_changed,
    toggle_completion,
    delete_task,
    begin_inline_edit,
    begin_drag,
    rubber_band,
    end_rubber_band,
    reorder
};

struct PointerAction {
    PointerActionKind kind = PointerActionKind::none;
    std::wstring task_id;
    std::wstring target_id;
    DropPosition position = DropPosition::before;
};

class PointerController {
public:
    explicit PointerController(SelectionModel& selection);
    void set_features(bool multi_select_enabled, bool rubber_band_enabled) noexcept;

    [[nodiscard]] PointerAction press(
        RowHitArea area,
        std::wstring_view task_id,
        PointF point,
        const std::vector<std::wstring>& visible_ids,
        bool ctrl,
        bool shift);
    [[nodiscard]] PointerAction move(
        PointF point,
        const std::vector<PointerRowBounds>& visible_rows);
    [[nodiscard]] PointerAction release(
        PointF point,
        const std::vector<PointerRowBounds>& visible_rows);
    void cancel() noexcept;
    [[nodiscard]] bool capturing() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::optional<RectF> rubber_band() const noexcept;

private:
    enum class Gesture { idle, blank_pending, rubber_band, dragging };
    void update_rubber_band(PointF point, const std::vector<PointerRowBounds>& visible_rows);

    SelectionModel& selection_;
    Gesture gesture_ = Gesture::idle;
    PointF start_{};
    PointF current_{};
    std::wstring source_id_;
    bool append_selection_ = false;
    bool capturing_ = false;
    bool multi_select_enabled_ = true;
    bool rubber_band_enabled_ = true;
    std::vector<std::wstring> initial_selection_;
};

}  // namespace desktop_todo
