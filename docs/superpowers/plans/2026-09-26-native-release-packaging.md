# Native Release Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce installable and portable x64/ARM64 releases, verify upgrade and uninstall behavior, and publish signed-or-clearly-unsigned artifacts to GitHub Releases from version tags.

**Architecture:** CMake produces deterministic native binaries and resource metadata; architecture-specific installer scripts package per-user installations without elevation. A release verification script is the single gate used locally and in GitHub Actions before artifacts and SHA-256 checksums are uploaded.

**Tech Stack:** C++20, CMake, MSVC, Windows SDK, Inno Setup, PowerShell verification scripts, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-09-26-native-win32-widget-design.md`

## Global Constraints

- Plans 1 and 2 completion gates are prerequisites.
- Publish x64 and ARM64 application executables, installers, portable ZIPs, and `checksums.txt`.
- Installation is per-user and must not require administrator rights.
- Runtime remains offline and must not contain update checks, telemetry, remote URLs, browser, or PowerShell dependencies.
- Portable application target remains below 5 MB; installer and ZIP sizes are recorded separately.
- Uninstall defaults to preserving user data and offers an explicit data-removal option.
- Unsigned output is allowed only when release notes clearly warn about SmartScreen; signing support must be wired but optional.
- Every production change starts with a failing verification and ends with a focused commit.

## Review Focus

- Upgrade over an older version must preserve `data.json`, backups, settings, and autostart intent; Task 3 pins this with an isolated profile.
- Uninstall must never delete user data unless the explicit removal option is selected; Task 3 tests both branches.
- A mismatched architecture or missing file must block publication rather than produce a partial release; Task 4 pins matrix aggregation.
- Secrets and certificates must never be printed or included in artifacts; Task 4 checks logs and artifact contents around the signing step.
- Tag, executable version, installer version, filenames, and release title must agree exactly; Tasks 1 and 4 enforce one version source.

---

### Task 1: Version Resources, Manifest, and Release Metadata

**Files:**
- Create: `native/resources/app.manifest`
- Create: `native/resources/version.rc.in`
- Create: `native/resources/resource.h`
- Create: `native/resources/app.ico`
- Create: `cmake/Version.cmake`
- Create: `native/tests/release/resource-metadata.ps1`
- Modify: `native/CMakeLists.txt`
- Modify: `CMakeLists.txt` if introduced at repository root

**Interfaces:**
- Consumes: release version passed as `DESKTOP_TODO_VERSION`, defaulting to a documented development version.
- Produces: one authoritative semantic version exposed in file/product resources, manifest metadata, `DesktopTodoList --version`, and installer configuration.

- [x] **Step 1: Write a failing resource metadata test**

  Assert executable architecture, requested execution level `asInvoker`, PerMonitorV2 DPI declaration, supported Windows GUIDs, product name, company placeholder, semantic version, original filename, and matching `--version` output.

- [x] **Step 2: Run the metadata test and confirm failure**

  Run: `powershell -ExecutionPolicy Bypass -File native/tests/release/resource-metadata.ps1 -Exe <x64-release-exe> -Version 2.0.0`.

  Expected: FAIL because resources and version CLI are incomplete.

- [x] **Step 3: Implement generated resources and version plumbing**

  Keep version data generated from one CMake value. Use the existing approved product icon if one exists; otherwise create a minimal repository-owned ICO with required 16/20/24/32/48/256 sizes before release review.

- [x] **Step 4: Rebuild and rerun metadata verification**

  Expected: all resource fields and `--version` match exactly.

- [x] **Step 5: Commit**

  ```powershell
  git add CMakeLists.txt cmake native/resources native/tests/release native/CMakeLists.txt
  git commit -m "build: add native release metadata"
  ```

### Task 2: Architecture-Specific Installers and Portable Packages

**Files:**
- Create: `installer/DesktopTodoList.iss`
- Create: `installer/messages.zh-CN.isl`
- Create: `installer/build-installer.ps1`
- Create: `scripts/package-portable.ps1`
- Create: `native/tests/release/package-contents.ps1`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: signed or unsigned Release executable, architecture, version, license/readme files.
- Produces: `DesktopTodoList-{arch}-Setup.exe` and `DesktopTodoList-{arch}-portable.zip` with stable filenames and no unrelated files.

- [x] **Step 1: Write failing package-content checks**

  Assert architecture-correct executable, README, privacy notice, license, no HTML/JS/PowerShell runtime files, no PDB in public package, no remote URL configuration, no elevation request, and portable unarchive/run behavior.

- [x] **Step 2: Run packaging checks and confirm failure**

  Run: the portable packaging script followed by `package-contents.ps1`.

  Expected: FAIL because scripts and packages do not exist.

- [x] **Step 3: Implement per-user Inno Setup and portable packaging**

  Set `PrivilegesRequired=lowest` and install under `{localappdata}\Programs\DesktopTodoList`. The x64 package uses `ArchitecturesAllowed=x64compatible and not arm64`; the ARM64 package uses `ArchitecturesAllowed=arm64`. Create Start Menu and uninstall entries, register the AppUserModelID/shortcut required for Toast, and leave autostart disabled until the user enables it.

- [x] **Step 4: Build and inspect x64 packages locally**

  Expected: installer runs without UAC, portable package launches without installation, and content checks pass. ARM64 packaging may be created by cross-build now and executed later on matching hardware.

- [x] **Step 5: Commit**

  ```powershell
  git add installer scripts native/tests/release .gitignore
  git commit -m "build: package native Windows releases"
  ```

### Task 3: Install, Upgrade, and Uninstall Verification

**Files:**
- Create: `native/tests/release/install-lifecycle.ps1`
- Create: `native/tests/release/test-profile.ps1`
- Create: `docs/acceptance/install-lifecycle.md`
- Modify: `installer/DesktopTodoList.iss`

**Interfaces:**
- Consumes: installer from Task 2 and an isolated test-local application data root supplied by test-only command-line/environment override.
- Produces: repeatable lifecycle evidence for clean install, in-place upgrade, default uninstall, and uninstall-with-data-removal.

- [x] **Step 1: Write the failing lifecycle harness**

  Assert no elevation, shortcut/AppUserModelID creation, application launch, seeded data persistence across upgrade, autostart command updating to the installed path, default uninstall preserving data, explicit cleanup removing only the exact DesktopTodoList data directory, and no deletion outside the isolated profile.

- [x] **Step 2: Run against the installer and record failing lifecycle points**

  Expected: nonzero until every automatable lifecycle condition passes; ARM64 execution is SKIP on non-ARM64 hardware with reason.

- [x] **Step 3: Adjust installer lifecycle behavior only**

  Do not add an application self-updater. Ensure cleanup paths are literal, resolved under the expected isolated or `%LOCALAPPDATA%\DesktopTodoList` root, and opt-in.

- [x] **Step 4: Run clean, upgrade, preserve, and remove-data cases**

  Expected: all x64 cases pass and evidence is recorded; no unrelated profile file changes.

- [x] **Step 5: Commit**

  ```powershell
  git add installer native/tests/release docs/acceptance/install-lifecycle.md
  git commit -m "test: verify installer lifecycle"
  ```

### Task 4: GitHub Actions Build, Sign, and Release

**Files:**
- Create: `.github/workflows/native-ci.yml`
- Create: `.github/workflows/native-release.yml`
- Create: `scripts/sign-artifact.ps1`
- Create: `scripts/write-checksums.ps1`
- Create: `native/tests/release/release-manifest.ps1`

**Interfaces:**
- Consumes: Git tag `vMAJOR.MINOR.PATCH`; optional signing secrets; build and packaging scripts from prior tasks.
- Produces: six architecture-specific public artifacts plus `checksums.txt`, with release publication blocked unless the complete manifest passes.

- [x] **Step 1: Write a failing release-manifest test**

  Assert exact filenames, both architectures, PE machine types, matching embedded/tag versions, nonempty packages, SHA-256 line for every artifact, no extra public file, and signature status recorded as either valid or explicitly unsigned.

- [x] **Step 2: Add CI in non-publishing mode and observe expected failure**

  Run the workflow-equivalent scripts locally for x64 and inspect workflow syntax. Expected: manifest fails until both matrix outputs are aggregated.

- [x] **Step 3: Implement CI and tag-release workflows**

  `native-ci.yml` builds/tests x64 and cross-builds ARM64 on pushes and pull requests. `native-release.yml` runs only for `v*` tags, validates tag syntax, signs when secrets exist without echoing secret material, packages both architectures, aggregates all artifacts, runs the manifest gate, then creates the GitHub Release.

- [x] **Step 4: Validate with a non-publishing workflow run or local action equivalent**

  Expected: complete artifact manifest passes; a deliberately removed ARM64 file or mismatched version makes the release job fail before upload.

- [x] **Step 5: Commit**

  ```powershell
  git add .github/workflows scripts native/tests/release
  git commit -m "ci: publish native Windows releases"
  ```

### Task 5: User Documentation, Privacy, and Migration

**Files:**
- Modify: `README.md`
- Create: `docs/privacy.md`
- Create: `docs/install.md`
- Create: `docs/migrate-from-web-version.md`
- Create: `docs/troubleshooting.md`
- Create: `native/tests/release/documentation-check.ps1`

**Interfaces:**
- Consumes: final paths, filenames, shortcuts, data locations, and fallback behaviors from all prior tasks.
- Produces: user-facing install/use/uninstall/migration/privacy guidance matching actual artifacts.

- [x] **Step 1: Write a failing documentation check**

  Require download choices, x64/ARM64 guidance, portable use, SmartScreen warning for unsigned builds, data and backup paths, export/import migration, tray recovery, `Ctrl+Alt+T`, `Ctrl+Alt+L`, uninstall data behavior, offline/no-telemetry statement, and troubleshooting for notification/hotkey/autostart failures.

- [x] **Step 2: Run the check and confirm failure**

  Run: `powershell -ExecutionPolicy Bypass -File native/tests/release/documentation-check.ps1`.

- [x] **Step 3: Write concise end-user documentation**

  State clearly that automatic extraction of the old Edge `localStorage` is not performed: users export JSON from the old version and import it in the native version.

- [x] **Step 4: Run documentation and package-content checks**

  Expected: all required topics are present and packaged documentation uses the same version-independent filenames.

- [x] **Step 5: Commit**

  ```powershell
  git add README.md docs native/tests/release
  git commit -m "docs: document native installation and migration"
  ```

### Task 6: Release Verification and Requirement Evidence

**Files:**
- Create: `scripts/verify-native-release.ps1`
- Create: `docs/acceptance/native-requirements.md`
- Create: `docs/acceptance/release-checklist.md`
- Modify: `tests/run-all.ps1`

**Interfaces:**
- Consumes: build outputs, all tests, package checks, acceptance evidence, and documentation checks.
- Produces: one nonzero-on-failure release gate and a requirement-by-requirement evidence matrix.

- [x] **Step 1: Create the failing final release gate**

  Verify all CTest results, x64 widget acceptance, ARM64 artifact architecture, package contents, portable executable size below 5 MB, installer and ZIP sizes recorded, no runtime network/browser/PowerShell dependency, complete docs, checksums, and each FR/NFR mapped exactly once to PASS, FAIL, or justified SKIP.

- [x] **Step 2: Run the gate and capture all remaining failures**

  Run: `powershell -ExecutionPolicy Bypass -File scripts/verify-native-release.ps1 -Version 2.0.0 -Artifacts <path>`.

  Expected: nonzero until all automatable checks pass and evidence rows exist.

- [x] **Step 3: Resolve verification gaps without weakening checks**

  Mark unavailable real ARM64, Windows 10, DPI, notification, or accessibility checks as “待对应环境验收” with environment and owner; never convert them to PASS using mocks.

- [ ] **Step 4: Run the complete release gate from a clean build directory**

  Expected: zero exit code; all automatable rows PASS; remaining manual rows are explicit and release-visible.

- [x] **Step 5: Commit**

  ```powershell
  git add scripts tests/run-all.ps1 docs/acceptance
  git commit -m "test: add native release verification gate"
  ```

### Task 7: Retire the Legacy Runtime After Native Acceptance

**Files:**
- Remove from active distribution: `src/`
- Remove from active distribution: `host/`
- Remove from active distribution: `启动待办.bat`
- Archive or remove obsolete tests only after equivalent native evidence exists: `tests/*.js`, `tests/test-server.ps1`, `tests/test-window.ps1`
- Modify: `tests/run-all.ps1`
- Modify: `docs/需求文档.md`
- Modify: `docs/acceptance/native-requirements.md`

**Interfaces:**
- Consumes: successful final release gate and explicit evidence mapping every legacy behavior to native coverage.
- Produces: a repository whose active product is unambiguously the native `.exe`, while legacy implementation remains recoverable from Git history.

- [ ] **Step 1: Add a failing legacy-runtime absence check to the release gate**

  Assert no public artifact contains `.html`, runtime `.js`, `launcher.ps1`, `msedge`, `WebView`, localhost server code, or batch-file launch instructions.

- [ ] **Step 2: Run the gate before retirement and confirm it identifies legacy distribution paths**

  Expected: FAIL until active build/package/docs references are removed.

- [ ] **Step 3: Remove only superseded runtime paths and update the PRD implementation clauses**

  Preserve requirement behavior. Replace obsolete NFR-04/NFR-09/NFR-10 and storage/architecture sections with the approved native design, while retaining history through Git rather than an active legacy folder.

- [ ] **Step 4: Run the full clean release verification again**

  Expected: release gate passes, native JSON migration fixture still passes, and no browser-era runtime file appears in public artifacts.

- [ ] **Step 5: Commit**

  ```powershell
  git add -A src host tests docs README.md 启动待办.bat
  git commit -m "refactor: retire browser-based runtime"
  ```

## Plan 3 Completion Gate

- Clean builds produce architecture-correct x64 and ARM64 executables, installers, portable ZIPs, and checksums.
- Per-user install, upgrade, default uninstall, and opt-in data removal are verified.
- GitHub tag workflows block partial or version-mismatched releases.
- Documentation accurately covers installation, migration, privacy, recovery, and unsigned-build warnings.
- The final release gate passes without hiding unavailable real-device checks.
