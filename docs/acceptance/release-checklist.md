# Native release checklist

Run `scripts/verify-native-release.ps1 -Version <version> -Artifacts <complete-release-directory>` from a clean x64 Release build. The release directory must contain both architectures' application executable, installer, portable ZIP, and `checksums.txt`; each executable, package, version, architecture, and checksum is verified before a zero exit code is reported.

## Automated release gate

- [ ] Clean x64 Release build and complete CTest suite pass.
- [ ] Native widget acceptance report is generated for this gate run.
- [ ] x64 and ARM64 PE architecture/version metadata match the requested version.
- [ ] x64 and ARM64 portable ZIPs contain the application, README, privacy notice, MIT license, and all three user guides.
- [ ] Portable application executables are each below 5 MB.
- [ ] Installer and portable ZIP sizes are recorded by the gate.
- [ ] Public package/source checks find no HTML/JavaScript runtime, browser/WebView, localhost service, PowerShell dependency, or unexpected network URL.
- [ ] Documentation checks and the unique FR/NFR evidence matrix pass.
- [ ] Release manifest contains exactly six payload files and checksum entries, with signature state explicitly `valid` or `unsigned`.

## Manual environment checks

The evidence matrix and native widget report identify each unverified item, its required environment, and its owner. These are not inferred from unit tests: Windows 10 1809 compatibility, physical high-DPI/multi-monitor behavior, actual Windows notification presentation, live tray/hotkey/autostart interaction, accessibility with Narrator/Inspect, and cold-start/idle-memory measurements need a human tester and matching environment. Do not convert a `SKIP` to `PASS` without recording the actual observation.

The local Visual Studio installation has no usable ARM64 compiler. Genuine ARM64 executables, installer, and portable package therefore remain **待对应环境验收**. Required environment: a GitHub `windows-2022` runner with the Visual Studio ARM64 build component (or real ARM64 hardware/toolchain). Owner: release maintainer. The release gate must remain failed until these real artifacts pass architecture/version checks; synthetic PE fixtures are test-only and are not release evidence.

## Publication review

- [ ] Review the generated gate report and all remaining `SKIP` items.
- [ ] Confirm release tag `v<version>`, artifacts, `checksums.txt`, and GitHub release title match.
- [ ] If unsigned, keep the SmartScreen warning in release notes; if signed, validate Authenticode status and protect signing secrets.
- [ ] Publish only after the CI release workflow's manifest gate succeeds.
