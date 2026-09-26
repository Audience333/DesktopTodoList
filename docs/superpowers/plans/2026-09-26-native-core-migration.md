# Native Core Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a tested C++20 domain and persistence core that preserves DesktopTodoList v1.2 behavior and imports existing schema-version-1 JSON without any browser dependency.

**Architecture:** A pure domain library owns task rules, queries, selection, undo, and reminders. A Windows persistence library owns JSON translation, transactional import, atomic disk writes, and seven-day backups; the UI will consume both through stable C++ interfaces in the next plan.

**Tech Stack:** C++20, CMake 3.25+, MSVC, CTest, Windows SDK, Windows Runtime JSON APIs, Win32 file APIs

**Spec:** `docs/superpowers/specs/2026-09-26-native-win32-widget-design.md`

## Global Constraints

- Target Windows 10 1809+ and Windows 11 on x64 and ARM64.
- Do not add HTML, WebView, Edge, PowerShell, localhost, telemetry, or runtime network access.
- Do not add a third-party runtime dependency; build-only tools are allowed.
- Preserve the logical `schemaVersion: 1` task and settings format used by exported JSON.
- Use UTF-8 on disk and Unicode Win32 APIs at platform boundaries.
- Cold-start target is at most 1 second; the portable application target is below 5 MB.
- Keep `src/`, `host/`, and current tests unchanged as behavior references until native acceptance is complete.
- Every production change starts with a failing test and ends with a focused commit.

## Review Focus

- Invalid UTF-8, malformed JSON, or unsupported schema must preserve the source file and return a visible recovery status; Task 6 and Task 7 pin this behavior.
- Local times around DST transitions must not double-notify or silently lose a reminder; Task 5 pins reminder identity to stored UTC instants.
- Duplicate task IDs during merge must resolve deterministically without overwriting unrelated tasks; Task 6 pins the conflict policy.
- A crash between temporary-file flush and replacement must leave either the old or new valid state; Task 7 exercises interrupted-save artifacts.
- More than 1,000 tasks and highly compressed order values must stay deterministic and compact safely; Task 4 includes scale and compaction tests.

---

### Task 1: Native Build and Test Baseline

**Files:**
- Create: `CMakePresets.json`
- Create: `CMakeLists.txt`
- Create: `native/CMakeLists.txt`
- Create: `native/app/main.cpp`
- Create: `native/domain/build_info.h`
- Create: `native/domain/build_info.cpp`
- Create: `native/tests/CMakeLists.txt`
- Create: `native/tests/test_main.cpp`
- Create: `native/tests/test_support.h`
- Modify: `tests/run-all.ps1`

**Interfaces:**
- Consumes: Visual Studio Build Tools with Desktop C++ workload, CMake 3.25+, Windows 10 SDK.
- Produces: CMake targets `desktop_todo_core`, `DesktopTodoList`, `desktop_todo_tests`; test macros `TEST_CASE(name)`, `EXPECT_TRUE(value)`, and `EXPECT_EQ(actual, expected)`.

- [ ] **Step 1: Add a test-runner smoke test that intentionally references `desktop_todo::build_marker()`**

  Add `native/tests/test_main.cpp` with test name `build_marker_identifies_native_core` and assertion `desktop_todo::build_marker() == L"DesktopTodoList.Native"` before defining the function.

- [ ] **Step 2: Configure and run the native test to verify it fails**

  Run: `cmake --preset windows-x64` then `cmake --build --preset windows-x64-debug`.

  Expected: build fails because `build_marker` is undefined. If CMake or MSVC is missing, stop implementation and install the named prerequisites with user approval; do not replace the toolchain.

- [ ] **Step 3: Create the CMake targets, minimal test harness, and `std::wstring_view build_marker() noexcept`**

  The root `CMakeLists.txt` declares the project, enables CTest, and adds `native/`. Configure `/W4 /WX /permissive- /utf-8`, C++20, Unicode definitions, static MSVC runtime for Release, and CTest registration. Define `build_marker` in `native/domain/build_info.*`. `DesktopTodoList` may display an empty native window at this stage but must link without browser libraries.

- [ ] **Step 4: Run the native smoke test and existing regression suite**

  Run: `ctest --preset windows-x64-debug --output-on-failure` and `powershell -ExecutionPolicy Bypass -File tests/run-all.ps1`.

  Expected: native smoke test passes and the existing JavaScript/PowerShell baseline remains unchanged except documented environment skips.

- [ ] **Step 5: Commit**

  ```powershell
  git add CMakeLists.txt CMakePresets.json native tests/run-all.ps1
  git commit -m "build: establish native C++ test baseline"
  ```

### Task 2: Schema Types and Validation

**Files:**
- Create: `native/domain/types.h`
- Create: `native/domain/validation.h`
- Create: `native/domain/validation.cpp`
- Create: `native/tests/domain/validation_test.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: standard C++20 types only.
- Produces: `enum class Priority { low, medium, high }`, `TaskStatus`, `ViewKind`, `Theme`, `WindowLayer`; structs `Task`, `Settings`, `AppState`, `ValidationIssue`; functions `TaskValidation validate_task(Task, double fallback_order, Clock::time_point now)` and `StateValidation validate_state(AppState, Clock::time_point now)`.

- [ ] **Step 1: Write failing schema and sanitization tests**

  Add tests named `blank_title_is_rejected`, `title_is_trimmed_and_limited_to_200`, `tags_are_unique_and_limited_to_5`, `todo_clears_completed_at`, `done_supplies_completed_at`, `invalid_enums_use_documented_defaults`, and `unknown_schema_reports_unsupported`. Assert the exact defaults `medium`, `todo`, `system`, `normal`, and `weekStartsOn = 1` from the current JavaScript tests.

- [ ] **Step 2: Run only validation tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R validation --output-on-failure`.

  Expected: FAIL because the domain types and validators do not exist.

- [ ] **Step 3: Implement schema types and validators**

  Use `std::chrono::sys_time<std::chrono::milliseconds>` for persisted instants and `std::optional` for nullable fields. Preserve task ID and creation time when valid; validation must return repaired values plus explicit issues rather than silently discarding information.

- [ ] **Step 4: Run validation tests and the full native suite**

  Run: `ctest --preset windows-x64-debug --output-on-failure`.

  Expected: all tests pass with no compiler warnings.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/domain native/tests/domain native/CMakeLists.txt native/tests/CMakeLists.txt
  git commit -m "feat: add native schema validation"
  ```

### Task 3: Task Store, Commands, and Undo

**Files:**
- Create: `native/domain/task_store.h`
- Create: `native/domain/task_store.cpp`
- Create: `native/domain/commands.h`
- Create: `native/tests/domain/task_store_test.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Task`, `Settings`, `AppState`, validation functions from Task 2.
- Produces: `class TaskStore` with `add_task`, `update_task`, `set_completed`, `delete_tasks`, `clear_completed`, `undo`, `can_undo`, `reorder`, `state`, and `take_change`; `struct StoreChange { bool persisted; std::vector<std::wstring> affected_ids; }`.

- [ ] **Step 1: Write failing command and undo tests**

  Cover empty-title rejection, newest task insertion, immutable `id`/`createdAt`, completion timestamps, deleting one and many tasks, clearing completed with confirmation supplied by the caller, five-second undo success, undo expiry, and only the latest action being undoable.

- [ ] **Step 2: Run the store tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R task_store --output-on-failure`.

  Expected: FAIL because `TaskStore` is absent.

- [ ] **Step 3: Implement the minimal command store**

  Inject `std::function<Clock::time_point()> now` and `std::function<std::wstring()> new_id` so tests never depend on wall-clock time or random GUIDs. A rejected command must not mutate state or create an undo entry.

- [ ] **Step 4: Run store and validation tests**

  Run: `ctest --preset windows-x64-debug -R "validation|task_store" --output-on-failure`.

  Expected: all selected tests pass.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/domain native/tests/domain native/CMakeLists.txt native/tests/CMakeLists.txt
  git commit -m "feat: add native task store and undo"
  ```

### Task 4: Queries, Ordering, and Selection

**Files:**
- Create: `native/domain/task_query.h`
- Create: `native/domain/task_query.cpp`
- Create: `native/domain/selection_model.h`
- Create: `native/domain/selection_model.cpp`
- Create: `native/tests/domain/task_query_test.cpp`
- Create: `native/tests/domain/selection_model_test.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TaskStore::state()` and the enums from Task 2.
- Produces: `std::vector<TaskRef> query_tasks(const AppState&, const QuerySpec&, Clock::time_point)`; `void compact_order(std::vector<Task>&)`; `class SelectionModel` with `select_one`, `toggle`, `select_range`, `toggle_all_visible`, `move_focus`, `remove_missing`, and `snapshot`.

- [ ] **Step 1: Write failing query and selection tests**

  Assert today/week/all/completed boundaries, Monday/Sunday week starts, title/note case-insensitive search, incomplete-before-complete ordering, overdue/high-priority/due/manual precedence, Ctrl toggle, Shift range, Ctrl+A toggle, hidden selection retention, deleted-ID cleanup, and completion-checkbox independence. Add `query_1000_tasks_is_stable` and `dense_orders_compact_to_consecutive_values`.

- [ ] **Step 2: Run query and selection tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R "task_query|selection_model" --output-on-failure`.

  Expected: FAIL because query and selection interfaces do not exist.

- [ ] **Step 3: Implement deterministic queries and selection**

  Keep selection IDs and anchor/focus runtime-only. `compact_order` must preserve the visible manual order and assign finite consecutive doubles beginning at `1.0` when gaps fall below `1e-6` or magnitude exceeds `1e6`.

- [ ] **Step 4: Run all native domain tests**

  Run: `ctest --preset windows-x64-debug -R "validation|task_store|task_query|selection_model" --output-on-failure`.

  Expected: all tests pass; the 1,000-task query is deterministic across repeated runs.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/domain native/tests/domain native/CMakeLists.txt native/tests/CMakeLists.txt
  git commit -m "feat: add native queries and selection"
  ```

### Task 5: Reminder Engine

**Files:**
- Create: `native/domain/reminder_engine.h`
- Create: `native/domain/reminder_engine.cpp`
- Create: `native/tests/domain/reminder_engine_test.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: validated tasks from Task 2 and store updates from Task 3.
- Produces: `ReminderBatch evaluate_startup_reminders(const AppState&, Clock::time_point)`; `ReminderBatch evaluate_tick_reminders(const AppState&, Clock::time_point)`; `Task reset_reminder_if_schedule_changed(const Task& before, Task after)`.

- [ ] **Step 1: Write failing reminder tests**

  Cover 0/5/10/30-minute lead times, startup aggregation, tick-by-tick delivery, `remindedAt` deduplication, schedule-change reset, completion and restoration without schedule change, a tick after sleep crossing the deadline, disabled reminders, malformed dates rejected by validation, and two local times mapping to distinct UTC instants across DST fallback.

- [ ] **Step 2: Run reminder tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R reminder_engine --output-on-failure`.

  Expected: FAIL because reminder functions are undefined.

- [ ] **Step 3: Implement reminder evaluation as pure UTC-instant comparisons**

  Return task IDs and display payloads without invoking Windows notification APIs. Marking `remindedAt` remains an explicit application-layer command so notification delivery and state mutation can be coordinated.

- [ ] **Step 4: Run all domain tests**

  Run: `ctest --preset windows-x64-debug -R "domain|reminder" --output-on-failure`.

  Expected: all domain tests pass.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/domain native/tests/domain native/CMakeLists.txt native/tests/CMakeLists.txt
  git commit -m "feat: add native reminder engine"
  ```

### Task 6: JSON Codec and Transactional Import/Export

**Files:**
- Create: `native/persistence/json_codec.h`
- Create: `native/persistence/json_codec.cpp`
- Create: `native/persistence/import_export.h`
- Create: `native/persistence/import_export.cpp`
- Create: `native/tests/persistence/json_codec_test.cpp`
- Create: `native/tests/persistence/import_export_test.cpp`
- Create: `native/tests/fixtures/schema-v1-export.json`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AppState` and validators from Task 2.
- Produces: `DecodeResult decode_state_utf8(std::span<const std::byte>)`; `std::vector<std::byte> encode_state_utf8(const AppState&)`; `ImportResult prepare_import(const AppState& current, std::span<const std::byte> source, ImportMode mode)` where `ImportMode` is `merge` or `replace` and no mutation occurs until the caller accepts `ImportResult::candidate`.

- [ ] **Step 1: Write failing codec and import tests**

  Assert round-trip fidelity, Chinese text, emoji, CRLF notes, unknown fields ignored, invalid UTF-8 rejected, malformed JSON rejected, schema other than `1` rejected, empty-title task dropped with an issue, replace leaves current state untouched on failure, merge adds new IDs, and duplicate IDs choose the item with later `updatedAt` while preserving unrelated tasks.

- [ ] **Step 2: Run persistence codec tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R "json_codec|import_export" --output-on-failure`.

  Expected: FAIL because codec and import APIs are absent.

- [ ] **Step 3: Implement codec and two-phase import**

  Use Windows Runtime JSON parsing behind this module only. Convert UTF-8 strictly; never accept replacement characters for malformed byte sequences. Encoding must be deterministic enough for stable fixture comparisons, but object member order is not a public contract.

- [ ] **Step 4: Run persistence and domain tests**

  Run: `ctest --preset windows-x64-debug --output-on-failure`.

  Expected: all tests pass, including the checked-in v1 JSON fixture.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/persistence native/tests/persistence native/tests/fixtures native/CMakeLists.txt native/tests/CMakeLists.txt
  git commit -m "feat: add schema-compatible JSON import"
  ```

### Task 7: Atomic Repository and Seven-Day Backups

**Files:**
- Create: `native/persistence/state_repository.h`
- Create: `native/persistence/state_repository.cpp`
- Create: `native/persistence/file_system.h`
- Create: `native/persistence/win32_file_system.cpp`
- Create: `native/tests/persistence/state_repository_test.cpp`
- Create: `native/tests/persistence/fake_file_system.h`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: JSON codec from Task 6.
- Produces: `class IFileSystem`; `class StateRepository` with `LoadResult load()`, `SaveResult save(const AppState&)`, `BackupResult ensure_daily_backup(const AppState&, LocalDate today)`, and `ExportResult preserve_corrupt_source()`.

- [ ] **Step 1: Write failing repository fault tests**

  Assert initial load, atomic temp/write/flush/replace order, failure before replace retaining old data, orphan temp ignored at startup, corrupt main restored from newest valid backup, corrupt files preserved with a timestamped name, all-invalid input producing safe empty state without overwrite, one backup per day, and exactly seven newest backup dates retained.

- [ ] **Step 2: Run repository tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R state_repository --output-on-failure`.

  Expected: FAIL because repository interfaces are absent.

- [ ] **Step 3: Implement repository over injected file-system operations**

  The production adapter must use same-directory temporary files, `FlushFileBuffers`, and `ReplaceFileW` with a safe fallback when the target does not yet exist. Tests use `fake_file_system.h` to inject faults after each operation.

- [ ] **Step 4: Run all native tests and a real temporary-directory integration test**

  Run: `ctest --preset windows-x64-debug --output-on-failure`.

  Expected: all unit and temporary-directory tests pass; no test writes to the user's real `%LOCALAPPDATA%`.

- [ ] **Step 5: Commit**

  ```powershell
  git add native/persistence native/tests/persistence native/CMakeLists.txt native/tests/CMakeLists.txt
  git commit -m "feat: add atomic native persistence"
  ```

### Task 8: Core Application Service and Migration Gate

**Files:**
- Create: `native/application/app_service.h`
- Create: `native/application/app_service.cpp`
- Create: `native/tests/application/app_service_test.cpp`
- Create: `native/tests/native-core-acceptance.ps1`
- Modify: `native/app/main.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/CMakeLists.txt`
- Modify: `tests/run-all.ps1`

**Interfaces:**
- Consumes: `TaskStore`, query/selection/reminder services, and `StateRepository` from Tasks 3–7.
- Produces: `class AppService` with `start`, command methods mirroring `TaskStore`, `prepare_import`, `accept_import`, `export_to`, `reset_to_defaults`, `tick_reminders`, `flush`, and `snapshot`; event callback `std::function<void(const AppEvent&)>` for the UI plan.

- [ ] **Step 1: Write failing orchestration acceptance tests**

  Test startup from fixture, startup backup, command-to-debounced-save flow, forced flush on shutdown, transactional import approval, confirmed factory reset with a pre-reset backup, reminder batch then persisted `remindedAt`, save-error event without state loss, and a 1,000-task query benchmark below 200 ms in Release on the build host.

- [ ] **Step 2: Run the application tests and confirm failure**

  Run: `ctest --preset windows-x64-debug -R app_service --output-on-failure`.

  Expected: FAIL because `AppService` does not exist.

- [ ] **Step 3: Implement application orchestration and the native-core acceptance script**

  Keep the executable headless-capable with `--verify-core <fixture>`, returning zero only when it can load, query, save to a temporary target, reload, and compare the logical state.

- [ ] **Step 4: Run Debug tests, Release acceptance, and existing regressions**

  Run: `cmake --build --preset windows-x64-release`, `ctest --preset windows-x64-release --output-on-failure`, `powershell -ExecutionPolicy Bypass -File native/tests/native-core-acceptance.ps1`, and `powershell -ExecutionPolicy Bypass -File tests/run-all.ps1`.

  Expected: native core passes; Release benchmark meets 200 ms on the recorded host; old suite remains green or explicitly skipped for environment limitations.

- [ ] **Step 5: Commit**

  ```powershell
  git add native tests/run-all.ps1
  git commit -m "feat: complete native application core"
  ```

## Plan 1 Completion Gate

- The native core builds and tests on x64 without invoking any browser or PowerShell at runtime.
- Existing schema-version-1 JSON fixtures import successfully.
- Atomic-save fault tests and seven-day backup tests pass.
- Domain behavior is covered for CRUD, sorting, selection, undo, reminders, and import.
- `AppService` is stable enough for the UI plan to consume without reaching into persistence or domain internals.
