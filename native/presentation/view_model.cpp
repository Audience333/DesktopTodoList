#include "presentation/view_model.h"

#include <algorithm>
#include <cwctype>

namespace desktop_todo {
namespace {

std::wstring lowercase(std::wstring_view text) {
    std::wstring result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return result;
}

void append_highlights(
    std::vector<HighlightSpan>& destination,
    std::wstring_view text,
    std::wstring_view search,
    HighlightField field,
    std::size_t field_index = 0) {
    if (search.empty()) return;
    const auto folded_text = lowercase(text);
    const auto folded_search = lowercase(search);
    std::size_t start = 0;
    while ((start = folded_text.find(folded_search, start)) != std::wstring::npos) {
        destination.push_back({field, field_index, start, folded_search.size()});
        start += std::max<std::size_t>(1, folded_search.size());
    }
}

std::chrono::sys_days local_day_of(Clock::time_point value, const QuerySpec& query) {
    return query.local_day
        ? query.local_day(value)
        : std::chrono::floor<std::chrono::days>(value + query.utc_offset);
}

std::chrono::sys_days week_start(std::chrono::sys_days today, int week_starts_on) {
    const auto weekday = std::chrono::weekday{today}.c_encoding();
    const auto start_day = week_starts_on == 0 ? 0U : 1U;
    const auto offset = (weekday + 7U - start_day) % 7U;
    return today - std::chrono::days{offset};
}

}  // namespace

ViewModel build_view_model(
    const AppState& state,
    const QuerySpec& query,
    Clock::time_point now) {
    ViewModel result;
    const auto tasks = query_tasks(state, query, now);
    result.rows.reserve(tasks.size());
    for (const auto& reference : tasks) {
        if (reference.task == nullptr) continue;
        const auto& task = *reference.task;
        TaskRowModel row{
            .id = task.id,
            .title = task.title,
            .note = task.note,
            .priority = task.priority,
            .status = task.status,
            .due_at = task.due_at,
            .overdue = task.status == TaskStatus::todo && task.due_at.has_value() &&
                *task.due_at < now};
        append_highlights(row.highlights, task.title, query.search, HighlightField::title);
        append_highlights(row.highlights, task.note, query.search, HighlightField::note);
        for (std::size_t index = 0; index < task.tags.size(); ++index) {
            append_highlights(row.highlights, task.tags[index], query.search,
                HighlightField::tag, index);
        }
        result.rows.push_back(std::move(row));
    }

    auto all_query = query;
    all_query.view = ViewKind::all;
    all_query.include_completed = true;
    const auto matching_tasks = query_tasks(state, all_query, now);
    const auto today = local_day_of(now, query);
    const auto start = week_start(today, query.week_starts_on);
    const auto end = start + std::chrono::days{7};
    for (const auto& reference : matching_tasks) {
        if (reference.task == nullptr) continue;
        const auto& task = *reference.task;
        if (task.status == TaskStatus::done) {
            ++result.counts.done;
            continue;
        }
        ++result.counts.all;
        if (!task.due_at.has_value()) continue;
        if (*task.due_at < now) {
            ++result.counts.today;
            ++result.counts.week;
            ++result.counts.overdue;
            continue;
        }
        const auto due_day = local_day_of(*task.due_at, query);
        if (due_day == today) ++result.counts.today;
        if (due_day >= start && due_day < end) ++result.counts.week;
    }
    return result;
}

}  // namespace desktop_todo
