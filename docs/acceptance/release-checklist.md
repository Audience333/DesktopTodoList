# Native release checklist

Run `scripts/verify-native-release.ps1 -Version <version> -Artifacts <complete-release-directory>` from a clean x64 Release build. The release directory must contain both architectures' application executable, installer, portable ZIP, and `checksums.txt`; each executable, package, version, architecture, and checksum is verified before a zero exit code is reported.

## Automated release gate

- [x] Clean x64 Release build and complete CTest suite pass (28/28).
- [x] Native widget acceptance report is generated for this gate run (18 PASS, 0 FAIL, 8 SKIP).
- [x] x64 and ARM64 PE architecture/version metadata match 2.0.0; real ARM64 outputs came from GitHub Actions `windows-2022` CI run 36548114765.
- [x] x64 and ARM64 portable ZIPs contain the application, README, privacy notice, MIT license, and all three user guides.
- [x] Portable application executables are each below 5 MB.
- [x] Installer and portable ZIP sizes are recorded by the gate.
- [x] Public package/source checks find no HTML/JavaScript runtime, browser/WebView, localhost service, PowerShell dependency, or unexpected network URL.
- [x] Documentation checks and the unique FR/NFR evidence matrix pass (79 requirement IDs).
- [x] Release manifest contains exactly six payload files and checksum entries, with signature state explicitly `unsigned`.

## Manual environment checks

The evidence matrix and native widget report identify each unverified item, its required environment, and its owner. These are not inferred from unit tests: Windows 10 1809 compatibility, physical high-DPI/multi-monitor behavior, actual Windows notification presentation, live tray/hotkey/autostart interaction, accessibility with Narrator/Inspect, and cold-start/idle-memory measurements need a human tester and matching environment. Do not convert a `SKIP` to `PASS` without recording the actual observation.

The local Visual Studio installation has no usable ARM64 compiler. Genuine ARM64 executable, installer, and portable package were produced and passed architecture/version/package checks on GitHub Actions `windows-2022`; executing the ARM64 app still requires matching ARM64 hardware. Synthetic PE fixtures remain test-only and are not release evidence.

## Publication review

- [x] Review the generated gate report and all remaining `SKIP` items; 13 checks pass, none fail, and six environment checks remain explicitly skipped.
- [ ] Confirm release tag `v<version>`, artifacts, `checksums.txt`, and GitHub release title match.
- [ ] If unsigned, keep the SmartScreen warning in release notes; if signed, validate Authenticode status and protect signing secrets.
- [ ] Publish only after the CI release workflow's manifest gate succeeds.
