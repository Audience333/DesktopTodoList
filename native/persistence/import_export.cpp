#include "persistence/import_export.h"

#include <algorithm>

namespace desktop_todo {

ImportResult prepare_import(
    const AppState& current,
    std::span<const std::byte> source,
    ImportMode mode) {
    auto decoded = decode_state_utf8(source);
    if (!decoded.state.has_value()) {
        return {std::nullopt, std::move(decoded.issues), std::move(decoded.error), 0, mode};
    }
    if (mode == ImportMode::replace) {
        const auto count = decoded.state->tasks.size();
        return {std::move(decoded.state), std::move(decoded.issues), {}, count, mode};
    }

    AppState candidate = current;
    std::size_t added = 0;
    for (auto& incoming : decoded.state->tasks) {
        const auto existing = std::find_if(
            candidate.tasks.begin(), candidate.tasks.end(), [&incoming](const Task& task) {
                return task.id == incoming.id;
            });
        if (existing == candidate.tasks.end()) {
            candidate.tasks.push_back(std::move(incoming));
            ++added;
        } else if (incoming.updated_at > existing->updated_at) {
            *existing = std::move(incoming);
        }
    }
    return {std::move(candidate), std::move(decoded.issues), {}, added, mode};
}

}  // namespace desktop_todo
