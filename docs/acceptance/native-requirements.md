# Native requirement evidence matrix

This matrix maps each requirement identifier in `docs/需求文档.md` exactly once. `PASS` means the named automated test or recorded acceptance evidence covers the native behavior; it does not claim that a separate manual check passed. `SKIP` rows name the environment and owner needed to finish real-device validation. The release verifier rejects missing/duplicate IDs, unknown statuses, and skips without an environment and owner.

| ID | Status | Evidence / follow-up |
|---|---|---|
| FR-01 | PASS | `native.task_store`, `native.app_service` — task creation and validation. |
| FR-02 | PASS | `native.editing`, `native.editor_host_smoke` — continuous native input. |
| FR-03 | PASS | `native.editing`, `native.editor_host_smoke` — edit/commit/cancel. |
| FR-04 | PASS | `native.task_store`, `native.app_service` — completion state. |
| FR-05 | PASS | `native.task_store`, `native.app_service` — restore pending state. |
| FR-06 | PASS | `native.task_store`, `native.app_service` — delete/undo model. |
| FR-07 | PASS | `native.task_store`, `native.task_query` — completed-task cleanup behavior. |
| FR-08 | PASS | `native.editing`, `native.validation` — task fields and validation. |
| FR-09 | PASS | `native.task_list_view`, `native.pointer_controller`, `native.selection_toolbar` — order and selection interaction. |
| FR-10 | PASS | `native.task_query`, `native.editing` — filtering/search model and input. |
| FR-20 | PASS | `native.task_query`, `native.renderer_state` — four native views. |
| FR-21 | PASS | `native.task_query`, `native.renderer_state` — pending counts. |
| FR-22 | PASS | `native.task_query`, `native.task_store` — deterministic ordering. |
| FR-23 | PASS | `native.task_query`, `native.renderer_state` — overdue state. |
| FR-24 | PASS | `native.renderer_smoke`, `native.renderer_state` — empty-state rendering. |
| FR-25 | PASS | `native.task_query`, `native.renderer_state` — summary counts. |
| FR-30 | PASS | `native.reminder_engine`, `native.notification_service` — due reminder scheduling. |
| FR-31 | SKIP | `native.notification_service` tests channel/fallback logic; real Windows notification presentation not run. Environment: dedicated Windows desktop with notifications enabled. Owner: release tester. |
| FR-32 | PASS | `native.reminder_engine`, `native.notification_service` — overdue reminder batching. |
| FR-33 | PASS | `native.reminder_engine`, `native.settings_panel` — configured advance interval. |
| FR-40 | PASS | `native.state_repository`, `native.app_service` — durable JSON state. |
| FR-41 | PASS | `native.state_repository`, `native.json_codec` — validation and recovery. |
| FR-42 | PASS | `native.import_export`, `native.data_transfer_dialog` — JSON transfer modes. |
| FR-43 | PASS | `native.state_repository` — daily backup retention. |
| FR-44 | PASS | `native.state_repository`, `native.app_service` — native filesystem failure reporting; browser quota handling is superseded by native storage. |
| FR-50 | PASS | `native.settings_panel`, `native.renderer_state` — system/light/dark theme state. |
| FR-51 | PASS | `native.widget_window_smoke`, `native.editing`, `native.hotkey_service` — keyboard commands. |
| FR-52 | SKIP | `native.tray_menu`, `docs/acceptance/native-widget.md`; live tray/close behavior not re-tested in this release gate. Environment: interactive Windows desktop. Owner: release tester. |
| FR-53 | PASS | `native.hotkey_service`, `native.window_behavior` — global show/hide registration and behavior. |
| FR-54 | PASS | `native.layout`, `native.renderer_state` — responsive layout model. |
| FR-55 | SKIP | `native.accessibility`, `native.widget_window_smoke` cover UIA semantics; screen-reader experience not measured. Environment: Windows with Narrator or Inspect. Owner: accessibility tester. |
| FR-56 | PASS | `native.widget_window_smoke`, `native.data_transfer_dialog` — JSON drop/import route. |
| FR-60 | PASS | `native.settings_panel`, `native.json_codec` — theme settings persistence. |
| FR-61 | PASS | `native.settings_panel`, `native.json_codec` — default view persistence. |
| FR-62 | PASS | `native.hotkey_service`, `native.settings_panel` — editable shortcuts and registration errors. |
| FR-63 | PASS | `native.tray_menu`, `native.window_behavior` — close-to-tray preference. |
| FR-64 | PASS | `native.app_service`, `native.state_repository` — reset confirmation/backup flow. |
| FR-65 | PASS | `native.autostart_service`, `native.settings_panel` — current-user autostart toggle. |
| FR-66 | PASS | `native.settings_panel`, `native.window_behavior`, `native.tray_menu` — window mode/layer controls. |
| FR-67 | PASS | `native.settings_panel`, `native.window_behavior` — interaction mode and recovery guard. |
| FR-68 | PASS | `native.hotkey_service`, `native.settings_panel` — configurable recovery shortcut. |
| FR-69 | PASS | `native.settings_panel`, `native.selection_model` — multi-select and rubber-band settings. |
| FR-70 | SKIP | `native.autostart_service`, `docs/acceptance/install-lifecycle.md` cover registration logic/lifecycle; writing the live user's Run key is intentionally avoided. Environment: disposable Windows user profile. Owner: release tester. |
| FR-71 | PASS | `native.widget_window_smoke`, `native.window_behavior` — native floating-window mode. |
| FR-72 | PASS | `native.window_behavior` — topmost layer state. |
| FR-73 | PASS | `native.window_behavior` — bottom layer state. |
| FR-74 | PASS | `native.window_behavior`, `native.hotkey_service` — interaction/selectable state. |
| FR-75 | PASS | `native.state_repository`, `native.layout` — geometry persistence and bounds model. |
| FR-76 | PASS | `native.json_codec`, `native.state_repository` — window state persistence. |
| FR-77 | PASS | `native.window_behavior`, `native.hotkey_service`, `native.tray_menu` — escape routes. |
| FR-78 | PASS | `native.tray_menu` — interaction state in tray tooltip/menu model. |
| FR-79 | PASS | `native.window_behavior`, `native.settings_panel` — mutually exclusive layer state. |
| FR-80 | PASS | `native.selection_model`, `native.pointer_controller` — single selection. |
| FR-81 | PASS | `native.selection_model`, `native.pointer_controller` — clear/toggle selection. |
| FR-82 | PASS | `native.selection_model`, `native.pointer_controller` — additive selection. |
| FR-83 | PASS | `native.selection_model`, `native.pointer_controller` — range selection. |
| FR-84 | PASS | `native.selection_model`, `native.selection_toolbar` — select all/none. |
| FR-85 | PASS | `native.pointer_controller`, `native.selection_model` — rubber-band selection. |
| FR-86 | PASS | `native.pointer_controller`, `native.selection_model` — keyboard selection model. |
| FR-87 | PASS | `native.selection_toolbar` — selected count. |
| FR-88 | PASS | `native.selection_toolbar`, `native.task_store` — batch completion. |
| FR-89 | PASS | `native.selection_toolbar`, `native.task_store` — batch restore. |
| FR-90 | PASS | `native.selection_toolbar`, `native.task_store` — batch delete/undo. |
| FR-91 | PASS | `native.selection_toolbar`, `native.task_store` — batch priority. |
| FR-92 | PASS | `native.selection_toolbar`, `native.task_store` — batch due date. |
| FR-93 | PASS | `native.selection_toolbar`, `native.task_store` — batch tags. |
| FR-94 | PASS | `native.pointer_controller`, `native.widget_window_smoke` — checkbox and selection separation. |
| FR-95 | PASS | `native.selection_model`, `native.selection_toolbar` — preserve hidden selections across views. |
| FR-96 | PASS | `native.selection_model`, `native.task_store` — clear deleted IDs. |
| NFR-01 | SKIP | `native.task_list_view`, `native.renderer_smoke` cover 1,000-item rendering; cold-start and idle-memory thresholds not measured in this run. Environment: clean interactive Windows desktop. Owner: performance tester. |
| NFR-02 | PASS | `native.task_list_view`, `native.renderer_smoke` — large-list virtualization/rendering. |
| NFR-03 | SKIP | `native.layout`, `native.renderer_state` cover DPI/bounds model; Windows 10 1809 and 125/150/200% physical-display checks remain. Environment: Windows 10 1809+ and multi-DPI displays. Owner: compatibility tester. |
| NFR-04 | PASS | Native Win32/C++ runtime; package checks reject HTML/JS, browser, WebView, localhost and PowerShell runtime files. |
| NFR-05 | PASS | `native.state_repository`, `native.app_service` — atomic save and recovery paths. |
| NFR-06 | PASS | Package/source verification rejects non-manifest URLs and remote-runtime mechanisms; export JSON schema contains no install path. |
| NFR-07 | PASS | `README.md`, `docs/privacy.md` — no telemetry/upload and local-data disclosure. |
| NFR-08 | PASS | CMake target boundaries and native core/platform/presentation modules; no third-party runtime dependency in public packages. |
| NFR-09 | PASS | Per-user installer (`PrivilegesRequired=lowest`) and architecture-specific portable ZIP are verified by package checks. |
| NFR-10 | PASS | `native/tests/release/install-lifecycle.ps1` — default data preservation and explicit opt-in removal. |

SKIP is not a pass. Required environment checks remain release-visible in `release-checklist.md` until their named owner supplies evidence.
