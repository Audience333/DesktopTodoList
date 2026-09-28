# Native Widget UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the actual borderless native Windows desktop widget with complete task interaction, tray integration, reminders, window layering, and click-through escape paths.

**Architecture:** A Win32 application shell owns process and message-loop lifecycle, while focused presentation components own layout, Direct2D rendering, native text input, and virtualized task rows. Windows integrations sit behind narrow adapters and communicate with the core only through `AppService` events and commands.

**Tech Stack:** C++20, Win32, Direct2D, DirectWrite, Windows Imaging Component, Windows Runtime Toast APIs, CMake, CTest

**Spec:** `docs/superpowers/specs/2026-09-26-native-win32-widget-design.md`

## Global Constraints

- Plan 1 completion gate is a prerequisite; do not duplicate domain or persistence logic in UI code.
- The running program must not create a browser process, WebView, localhost listener, or PowerShell child process.
- Default window size is 360 x 480 logical pixels; minimum width is 320 logical pixels.
- Support Windows 10 1809+, Windows 11, x64, ARM64, and 125%/150%/200% DPI.
- Top, normal, and bottom modes are mutually exclusive.
- Click-through is unreleasable unless both `Ctrl+Alt+L` and the tray “Allow interaction” action restore input.
- Keyboard and screen-reader semantics must remain available for primary actions.
- Every production change starts with a failing test or an explicit failing Windows harness check and ends with a focused commit.

## Review Focus

- Disconnecting a monitor must never strand the widget off-screen; Task 2 tests work-area clamping and DPI conversion.
- A hotkey registration conflict must leave the prior shortcut active and explain the failure; Task 6 pins rollback behavior.
- Enabling click-through while the tray icon is unavailable must be refused; Task 7 tests escape-path availability before style mutation.
- IME composition, emoji, and Chinese text must survive inline edit and detail edit; Task 4 includes native input harness cases.
- Rendering/device loss during resize or sleep must recreate graphics resources without losing application state; Task 2 injects device-loss paths.

---

### Task 1: Win32 Lifecycle and Single Instance

**Files:**
- Create: `native/app/application.h`
- Create: `native/app/application.cpp`
- Create: `native/platform/windows/window_class.h`
- Create: `native/platform/windows/window_class.cpp`
- Create: `native/platform/windows/single_instance.h`
- Create: `native/platform/windows/single_instance.cpp`
- Create: `native/tests/platform/single_instance_test.cpp`
- Modify: `native/app/main.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `AppService` from Plan 1.
- Produces: `class Application { int run(HINSTANCE, int); }`; `class SingleInstance { AcquireResult acquire(); bool signal_existing(LaunchRequest); }`; one hidden message window and one widget window owned by the primary instance.

- [ ] **Step 1: Write failing single-instance protocol tests**

  Assert first-instance acquisition, second-instance `show` request, `.json` import-path request, malformed message rejection, and no data writes by the second instance.

- [ ] **Step 2: Run the focused tests and verify failure**

  Run: `ctest --preset windows-x64-debug -R single_instance --output-on-failure`.

  Expected: FAIL because lifecycle and IPC classes do not exist.

- [ ] **Step 3: Implement RAII Win32 lifecycle and second-instance signaling**

  Use a per-user named mutex plus `WM_COPYDATA` to the uniquely named hidden message window. Accept only a versioned UTF-16 payload no larger than 32 KiB, canonicalize import paths, and reject any command other than `show` or `import`. Register Unicode window classes and keep `wWinMain` limited to DPI setup, construction, `run`, and error reporting.

- [ ] **Step 4: Run tests and manually launch twice**

  Expected: only one process owns the widget; the second launch raises the existing widget and exits zero.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/app native/platform/windows native/tests/platform native/CMakeLists.txt
  git commit -m "feat: add native Win32 application shell"
  ```

### Task 2: DPI-Aware Window, Layout, Theme, and Rendering

**Files:**
- Create: `native/presentation/widget_window.h`
- Create: `native/presentation/widget_window.cpp`
- Create: `native/presentation/layout.h`
- Create: `native/presentation/layout.cpp`
- Create: `native/presentation/theme.h`
- Create: `native/presentation/theme.cpp`
- Create: `native/presentation/renderer.h`
- Create: `native/presentation/renderer.cpp`
- Create: `native/tests/presentation/layout_test.cpp`
- Create: `native/tests/presentation/renderer_state_test.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: widget HWND from Task 1 and settings snapshot from Plan 1.
- Produces: `LayoutResult calculate_layout(SizeF client, float dpi, LayoutMode)`; `RectI clamp_to_work_area(RectI saved, MonitorInfo)`; `class Renderer` with `create_device_resources`, `draw`, `discard_device_resources`; `ThemePalette resolve_theme(Theme, SystemTheme)`.

- [x] **Step 1: Write failing pure layout and renderer-state tests**

  Assert 360 x 480 default, 320 minimum width, 125/150/200% scaling, compact/expanded regions, off-screen recovery after monitor removal, system/light/dark/high-contrast palettes, reduced-motion flag, and graphics-device-loss recovery state.

- [x] **Step 2: Run presentation tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R "layout|renderer_state" --output-on-failure`.

  Expected: FAIL because presentation modules are absent.

- [x] **Step 3: Implement borderless DPI-aware window and Direct2D renderer**

  Use per-monitor-v2 DPI awareness, logical-pixel layout, `WM_NCHITTEST` resize edges, a draggable title region, and resource recreation on `D2DERR_RECREATE_TARGET`. Do not use undocumented WorkerW parenting.

- [x] **Step 4: Run tests and the window smoke harness at available DPI settings**

  Expected: no blurry bitmap scaling, no lost window after work-area change, and no state loss after renderer recreation.

- [x] **Step 5: Commit**

  ```powershell
  git add native/presentation native/tests/presentation native/CMakeLists.txt
  git commit -m "feat: render native floating widget"
  ```

### Task 3: Virtual Task List and View Navigation

**Files:**
- Create: `native/presentation/task_list_view.h`
- Create: `native/presentation/task_list_view.cpp`
- Create: `native/presentation/view_model.h`
- Create: `native/presentation/view_model.cpp`
- Create: `native/tests/presentation/task_list_view_test.cpp`
- Modify: `native/presentation/widget_window.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `AppService::snapshot`, `query_tasks`, and app events from Plan 1.
- Produces: `class ViewModel` translating snapshots to display rows; `VisibleRange calculate_visible_range(float scroll_y, float viewport_height, float row_height, size_t count, size_t overscan)`; hit-test results for checkbox, title, row, delete, and drag handle.

- [ ] **Step 1: Write failing view-model and virtualization tests**

  Cover four view counts, empty states, overdue color token, highlighted search spans, 0/1/200/1,000 rows, viewport overscan, stable scroll anchor after update, and non-overlapping hit targets.

- [ ] **Step 2: Run task-list tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R task_list_view --output-on-failure`.

- [ ] **Step 3: Implement view model, virtual list, scrolling, and view tabs**

  Render only visible rows plus two rows of overscan. Accessibility and keyboard focus must refer to logical task IDs rather than recycled visual indices.

- [ ] **Step 4: Run tests and Release rendering benchmark**

  Expected: 1,000-task snapshot updates and first visible render complete within the spec's 200 ms budget on the recorded host.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/presentation native/tests/presentation native/CMakeLists.txt
  git commit -m "feat: add virtual native task list"
  ```

### Task 4: Native Input, Editing, Search, and Details

**Files:**
- Create: `native/presentation/text_editor.h`
- Create: `native/presentation/text_editor.cpp`
- Create: `native/presentation/details_panel.h`
- Create: `native/presentation/details_panel.cpp`
- Create: `native/tests/presentation/editing_test.cpp`
- Create: `native/tests/windows/input-harness.ps1`
- Modify: `native/presentation/widget_window.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: command methods on `AppService`.
- Produces: native edit-host lifecycle `begin`, `commit`, `cancel`; detail draft `TaskPatch`; shortcuts `Ctrl+N`, `Ctrl+F`, `Esc`, `Delete`, `Alt+Up`, `Alt+Down`.

- [ ] **Step 1: Write failing editor-state and native input harness checks**

  Assert Enter creation and refocus, blank rejection, inline Enter save/Escape cancel/focus-loss save, title click not changing selection, double-click opening details, search focus, note/priority/due/reminder/tag edits, and Chinese IME/emoji/CRLF round-trip.

- [ ] **Step 2: Run editing tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R editing --output-on-failure`.

- [ ] **Step 3: Implement native edit controls and details panel**

  Use Unicode native edit controls for composition and clipboard behavior; custom drawing may frame them but must not reimplement an IME text editor.

- [ ] **Step 4: Run automated tests and interactive input harness**

  Expected: automated state tests pass; interactive items report PASS or an explicit environment SKIP, never a silent pass.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/presentation native/tests native/CMakeLists.txt
  git commit -m "feat: add native task editing"
  ```

### Task 5: Selection, Rubber Band, Batch Actions, and Drag Ordering

**Files:**
- Create: `native/presentation/pointer_controller.h`
- Create: `native/presentation/pointer_controller.cpp`
- Create: `native/presentation/selection_toolbar.h`
- Create: `native/presentation/selection_toolbar.cpp`
- Create: `native/tests/presentation/pointer_controller_test.cpp`
- Modify: `native/presentation/task_list_view.cpp`
- Modify: `native/presentation/widget_window.cpp`

**Interfaces:**
- Consumes: `SelectionModel` and reorder/batch commands from Plan 1; hit tests from Task 3.
- Produces: pointer actions `click`, `ctrl_click`, `shift_click`, `rubber_band`, `begin_drag`, `drop`; batch-toolbar model including hidden-selection count.

- [ ] **Step 1: Write failing pointer arbitration tests**

  Assert checkbox toggles completion without selection, row area selects, title begins edit, handle begins ordering, blank drag begins rubber band, Ctrl rubber band appends, Shift keyboard extends, hidden count is shown, batch completion/deletion/priority/due-date/tag changes apply once, and bulk delete restores as one undo action.

- [ ] **Step 2: Run pointer tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R pointer_controller --output-on-failure`.

- [ ] **Step 3: Implement pointer controller and batch toolbar**

  Use pointer capture only for active drag/rubber-band operations and always release it on cancellation, focus loss, or window destruction.

- [ ] **Step 4: Run presentation tests and interactive drag checks**

  Expected: all tests pass and drag/rubber-band operations do not interfere with window dragging or scrolling.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/presentation native/tests/presentation
  git commit -m "feat: add native multi-select interactions"
  ```

### Task 6: Tray, Close Behavior, and Global Show/Hide

**Files:**
- Create: `native/platform/windows/tray_icon.h`
- Create: `native/platform/windows/tray_icon.cpp`
- Create: `native/platform/windows/hotkey_service.h`
- Create: `native/platform/windows/hotkey_service.cpp`
- Create: `native/tests/platform/tray_menu_test.cpp`
- Create: `native/tests/platform/hotkey_service_test.cpp`
- Modify: `native/app/application.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: widget visibility commands and settings from Plan 1.
- Produces: `class TrayIcon` with `install`, `update_count`, `update_interaction_state`, `remove`; `class HotkeyService` with transactional `replace(HotkeyAction, HotkeyChord)`; default global show/hide `Ctrl+Alt+T`.

- [ ] **Step 1: Write failing tray-model and hotkey transaction tests**

  Assert menu check states, pending-count badge text, Explorer restart re-add, close-to-tray versus exit, show/hide toggle, default chord, conflict reporting, and failed replacement retaining the prior registered chord.

- [ ] **Step 2: Run platform tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R "tray_menu|hotkey_service" --output-on-failure`.

- [ ] **Step 3: Implement tray and hotkey adapters**

  Keep menu generation separate from Win32 dispatch so its state is unit-testable. On `TaskbarCreated`, reinstall the icon before enabling click-through operations.

- [ ] **Step 4: Run tests and an interactive Explorer-restart check**

  Expected: tray returns after Explorer restarts; failed hotkey changes leave a working recovery path.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/platform/windows native/tests/platform native/app native/CMakeLists.txt
  git commit -m "feat: add tray and global activation"
  ```

### Task 7: Window Layering and Click-Through Escape Paths

**Files:**
- Create: `native/platform/windows/window_behavior.h`
- Create: `native/platform/windows/window_behavior.cpp`
- Create: `native/tests/platform/window_behavior_test.cpp`
- Create: `native/tests/windows/window-behavior-harness.ps1`
- Modify: `native/app/application.cpp`
- Modify: `native/platform/windows/tray_icon.cpp`
- Modify: `native/platform/windows/hotkey_service.cpp`

**Interfaces:**
- Consumes: widget HWND, tray availability, and hotkey service from Task 6.
- Produces: `class WindowBehavior` with transactional `set_layer(WindowLayer)`, `set_click_through(bool)`, `restore_interaction`, `on_foreground_changed`, and `snapshot`; default escape chord `Ctrl+Alt+L`.

- [ ] **Step 1: Write failing state-machine and safety tests**

  Assert top/normal/bottom exclusivity, style-bit changes, foreground-change bottom correction, click-through requiring two available escape routes, tray restoration, hotkey restoration, optional 30-minute recovery, startup state restoration, and refusal when tray installation or hotkey registration has failed.

- [ ] **Step 2: Run window-behavior tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R window_behavior --output-on-failure`.

- [ ] **Step 3: Implement documented Win32 layer and style transitions**

  Apply style changes with `SetWindowLongPtrW` and `SetWindowPos(...SWP_FRAMECHANGED)`. Use documented Z-order APIs and a foreground WinEvent hook; never attach to undocumented desktop WorkerW windows.

- [ ] **Step 4: Run unit tests and real-window harness**

  Expected: both recovery paths independently restore interaction. Restricted or non-interactive environments must report explicit SKIP for system-level checks.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/platform/windows native/tests native/app
  git commit -m "feat: add safe native window modes"
  ```

### Task 8: Settings, Data Transfer, and Factory Reset

**Files:**
- Create: `native/presentation/settings_panel.h`
- Create: `native/presentation/settings_panel.cpp`
- Create: `native/presentation/data_transfer_dialog.h`
- Create: `native/presentation/data_transfer_dialog.cpp`
- Create: `native/tests/presentation/settings_panel_test.cpp`
- Create: `native/tests/presentation/data_transfer_dialog_test.cpp`
- Modify: `native/presentation/widget_window.cpp`
- Modify: `native/app/application.cpp`

**Interfaces:**
- Consumes: settings/import/export/reset commands from Plan 1 and platform services from Tasks 6–7.
- Produces: settings draft/validation/commit flow; native open/save dialogs; drag-and-drop JSON import; merge/replace confirmation; factory-reset confirmation.

- [ ] **Step 1: Write failing settings and data-transfer tests**

  Assert theme, default view, week start, close behavior, custom show/hide and recovery hotkeys, layer, click-through timeout, multi-select, rubber-band, and batch-toolbar settings. Assert failed hotkey registration rolls back the whole settings draft. Cover valid/invalid dropped JSON, merge/replace confirmation, canceled import leaving state untouched, export path errors, and factory reset requiring a second confirmation and creating a backup.

- [ ] **Step 2: Run focused tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R "settings_panel|data_transfer_dialog" --output-on-failure`.

- [ ] **Step 3: Implement settings and native data-management dialogs**

  Use Windows common open/save dialogs and `WM_DROPFILES` only for `.json` files. Validate the complete settings draft and all platform transitions before persisting it; show user-visible errors for rejected hotkeys, registry writes, imports, exports, and resets.

- [ ] **Step 4: Run tests and interactive file-dialog/drop checks**

  Expected: all state tests pass; native dialog and drop checks report PASS or explicit environment SKIP.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/presentation native/tests/presentation native/app
  git commit -m "feat: add native settings and data transfer"
  ```

### Task 9: Notifications, Autostart, and Accessibility

**Files:**
- Create: `native/platform/windows/notification_service.h`
- Create: `native/platform/windows/notification_service.cpp`
- Create: `native/platform/windows/autostart_service.h`
- Create: `native/platform/windows/autostart_service.cpp`
- Create: `native/presentation/accessibility_provider.h`
- Create: `native/presentation/accessibility_provider.cpp`
- Create: `native/tests/platform/notification_service_test.cpp`
- Create: `native/tests/platform/autostart_service_test.cpp`
- Create: `native/tests/presentation/accessibility_test.cpp`
- Modify: `native/app/application.cpp`

**Interfaces:**
- Consumes: reminder batches and settings commands from Plan 1.
- Produces: `NotificationResult show_reminder(const ReminderBatch&)`; `AutostartResult set_enabled(bool, std::filesystem::path executable)`; UI Automation fragments for visible logical controls and virtual list rows.

- [ ] **Step 1: Write failing adapter and accessibility tests**

  Assert startup aggregation text, tick notification payload, toast-to-tray fallback, notification failure not marking reminders delivered, exact HKCU value name `DesktopTodoList`, write failure reporting, focus order, accessible names/roles/states, and virtual-row identity stability.

- [ ] **Step 2: Run focused tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R "notification|autostart|accessibility" --output-on-failure`.

- [ ] **Step 3: Implement adapters, settings panel, and UI Automation surface**

  Mark `remindedAt` only after at least one notification channel reports success. Registry access remains HKCU-only and errors must return to the settings panel from Task 8.

- [ ] **Step 4: Run tests and Windows accessibility smoke checks**

  Expected: automated tests pass; Narrator/Inspect checks are recorded as PASS or environment SKIP.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/platform/windows native/presentation native/tests native/app
  git commit -m "feat: complete native Windows integrations"
  ```

### Task 10: Native Widget Acceptance Gate

**Files:**
- Create: `native/tests/windows/widget-acceptance.ps1`
- Create: `docs/acceptance/native-widget.md`
- Modify: `tests/run-all.ps1`
- Modify: `native/app/application.cpp`

**Interfaces:**
- Consumes: all prior tasks in Plans 1 and 2.
- Produces: repeatable acceptance report with `PASS`, `FAIL`, or `SKIP` for every native widget capability.

- [ ] **Step 1: Add a failing acceptance manifest**

  Include process/browser absence, cold launch at most 1 second, idle working set below 120 MB, single instance, CRUD, four views, editing, selection, drag order, import/export/reset, settings, tray, hotkeys, layers, click-through recovery, reminder fallback, autostart, accessibility, DPI, monitor recovery, shutdown flush, and 1,000-task performance.

- [ ] **Step 2: Run the harness and record the expected incomplete result**

  Run: `powershell -ExecutionPolicy Bypass -File native/tests/windows/widget-acceptance.ps1`.

  Expected: nonzero while any automatable item is FAIL; unavailable interactive environments are SKIP with a reason.

- [ ] **Step 3: Fix only integration gaps exposed by the acceptance run**

  Do not add new product behavior. Update `docs/acceptance/native-widget.md` with environment, architecture, OS build, DPI, and evidence for each manual item.

- [ ] **Step 4: Run full Debug and Release verification**

  Run: both x64 presets, full CTest, `tests/run-all.ps1`, and widget acceptance.

  Expected: all automatable checks pass; no unexplained skip or browser process exists.

- [ ] **Step 5: Commit**

  ```powershell
  git add native tests docs/acceptance/native-widget.md
  git commit -m "test: verify native desktop widget"
  ```

## Plan 2 Completion Gate

- The default launch surface is a true borderless native floating widget.
- No browser, WebView, localhost server, or PowerShell host participates at runtime.
- Core task behavior, native input, selection, tray, hotkeys, layers, and click-through recovery are integrated.
- All automatable acceptance checks pass; system checks have honest PASS/SKIP evidence.
