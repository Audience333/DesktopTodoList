# Native installer lifecycle acceptance

**Result:** PASS for the x64 Windows test profile on 2026-09-29.

The repeatable check is `native/tests/release/install-lifecycle.ps1`. It seeds a unique GUID profile below the Windows temporary directory, installs a prior-version test build, upgrades it in place, and exercises both uninstall choices. Test-only data and autostart overrides reject paths or registry names outside that profile; the test installer uses its own AppId and Start Menu group. The script removes its temporary profile, registry value, and shortcut when it exits.

Verified behaviors:

- Silent per-user installation succeeds without elevation and creates a Start Menu shortcut targeting the installed executable. The shortcut carries the same `DesktopTodoList.Native` AppUserModelID as the application.
- Enabling autostart points the current user's Run value to the installed executable. Launching the upgraded app refreshes a stale path to the current install location.
- Upgrading 1.9.0 to 2.0.0 preserves `data.json` and a seeded backup byte-for-byte.
- Default silent uninstall removes the program and its autostart entry while preserving user data.
- Explicit `/REMOVEUSERDATA` uninstall deletes only the exact `DesktopTodoList` data directory and preserves a marker outside it.
- All test-created artifacts are cleaned up after verification.

The application uses `%LOCALAPPDATA%\DesktopTodoList\data.json` as its canonical state file. If the earlier `state.json` file is found and `data.json` is absent, it is migrated by writing `data.json`; the original `state.json` remains as a recovery copy.

**Not covered here:** execution on physical ARM64 hardware, Windows versions other than the test host, and interactive accessibility/notification behavior. These remain separate environment-specific acceptance items and are not inferred from this x64 lifecycle pass.
