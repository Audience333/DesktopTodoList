#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "domain/types.h"
#include "platform/windows/hotkey_service.h"

#include <functional>
#include <string>
#include <vector>

namespace desktop_todo {

struct SettingsApplyApi {
    std::function<HotkeyReplaceResult(HotkeyAction, std::wstring_view)> replace_hotkey;
    std::function<bool(WindowLayer)> set_layer;
    std::function<bool(bool)> set_click_through;
    std::function<bool()> escape_routes_available;
    std::function<bool(const Settings&)> save;
    std::function<bool(bool)> set_auto_start;
};

struct SettingsApplyResult {
    bool success = false;
    bool rollback_complete = true;
    std::wstring error;
};

[[nodiscard]] std::vector<std::wstring> validate_settings_draft(const Settings& settings);
[[nodiscard]] SettingsApplyResult commit_settings_draft(
    const Settings& current, const Settings& draft, const SettingsApplyApi& api);

enum class SettingsTransferAction { import_json, export_json, reset_data };

class SettingsPanel {
public:
    using Commit = std::function<SettingsApplyResult(const Settings&, const Settings&)>;
    using Transfer = std::function<std::optional<Settings>(HWND, SettingsTransferAction)>;

    [[nodiscard]] bool show_modal(
        HWND owner, const Settings& initial, Commit commit, Transfer transfer);

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void create_controls();
    void update_controls(const Settings& settings);
    [[nodiscard]] Settings read_draft() const;
    void close(bool committed);

    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    HWND controls_[18]{};
    Settings initial_;
    Commit commit_;
    Transfer transfer_;
    bool closed_ = false;
    bool committed_ = false;
};

}  // namespace desktop_todo
