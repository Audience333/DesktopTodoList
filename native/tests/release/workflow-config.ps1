$ErrorActionPreference = 'Stop'
$ciPath = Join-Path $PSScriptRoot '..\..\..\.github\workflows\native-ci.yml'
$releasePath = Join-Path $PSScriptRoot '..\..\..\.github\workflows\native-release.yml'
$rendererTestPath = Join-Path $PSScriptRoot '..\presentation\renderer_smoke_test.cpp'
$releaseNotesPath = Join-Path $PSScriptRoot '..\..\..\docs\releases\v2.0.1.md'
$cmakePath = Join-Path $PSScriptRoot '..\..\..\CMakeLists.txt'
$ci = [IO.File]::ReadAllText((Resolve-Path $ciPath).Path)
$release = [IO.File]::ReadAllText((Resolve-Path $releasePath).Path)
$rendererTest = [IO.File]::ReadAllText((Resolve-Path $rendererTestPath).Path)
$cmake = [IO.File]::ReadAllText((Resolve-Path $cmakePath).Path)
$releaseNotes = [IO.File]::ReadAllText((Resolve-Path $releaseNotesPath).Path)

function Assert-Pattern([string]$Text, [string]$Pattern, [string]$Description) {
  if ($Text -notmatch $Pattern) { throw "Workflow configuration is missing: $Description" }
  Write-Host "[PASS] $Description"
}

Assert-Pattern $ci '(?m)^  pull_request:' 'PRs run native CI'
Assert-Pattern $ci '(?m)^\s*platform:\s*ARM64\s*$' 'CI cross-builds ARM64'
Assert-Pattern $cmake '(?m)^set\(DESKTOP_TODO_VERSION "2\.0\.1" CACHE STRING' 'Default project version is 2.0.1'
Assert-Pattern $ci '"-DDESKTOP_TODO_VERSION=2\.0\.1"' 'CI passes the full semantic version as one PowerShell argument'
Assert-Pattern $ci 'workflow-config\.ps1' 'CI checks release workflow configuration'
Assert-Pattern $ci 'documentation-check\.ps1' 'CI checks packaged user documentation'
Assert-Pattern $ci 'ctest --test-dir' 'CI runs the native test suite'
Assert-Pattern $ci "DESKTOP_TODO_RENDER_BUDGET_MODE = 'measure-only'" 'Hosted CI records rendering time without applying the desktop-only budget'
Assert-Pattern $ci 'SkipExecutableRun' 'CI skips execution only for ARM64 package checks'
Assert-Pattern $ci 'contents: read' 'CI token is read-only'

Assert-Pattern $release "tags:\s*\r?\n\s*- 'v\*'" 'Release runs only for pushed version tags'
Assert-Pattern $release 'RELEASE_VERSION[\s\S]*?GITHUB_ENV' 'Tag version is passed to CMake and packaging'
Assert-Pattern $release "DESKTOP_TODO_RENDER_BUDGET_MODE = 'measure-only'" 'Hosted release runner records rendering time without applying the desktop-only budget'
Assert-Pattern $release 'WINDOWS_SIGNING_CERTIFICATE_BASE64' 'Release signing certificate is supplied through a required secret'
Assert-Pattern $release 'WINDOWS_SIGNING_CERTIFICATE_PASSWORD' 'Release signing password is supplied through a required secret'
Assert-Pattern $release 'Require Authenticode signing secrets[\s\S]*?IsNullOrWhiteSpace\(\$env:WINDOWS_SIGNING_CERTIFICATE_BASE64\)' 'Release checks signing secrets before building artifacts'
Assert-Pattern $release 'actions/download-artifact@[0-9a-f]{40}[\s\S]*?merge-multiple: true' 'Pinned architecture artifacts are merged for aggregate verification'
Assert-Pattern $release 'persist-credentials: false' 'Release checkout does not retain write-capable credentials'
Assert-Pattern $release 'release-manifest\.ps1' 'Publishing is gated by the complete release manifest'
Assert-Pattern $release 'release-manifest\.ps1[\s\S]*?ghArgs[\s\S]*?release.*create' 'GitHub Release creation follows manifest validation'
Assert-Pattern $release 'docs/releases/v\$env:RELEASE_VERSION\.md' 'Release resolves versioned release notes'
Assert-Pattern $release '--notes-file.*\$notesPath' 'Release creation attaches the resolved release notes'
Assert-Pattern $release 'contents: write' 'Only the publish job receives release-write permission'
Assert-Pattern $releaseNotes '桌面快捷方式|desktop shortcut' 'Release notes mention the optional desktop shortcut'
Assert-Pattern $releaseNotes '自动启动.*默认关闭|autostart.*off' 'Release notes explain the autostart default'
Assert-Pattern $rendererTest 'if \(enforce_budget\) EXPECT_TRUE\(elapsed < std::chrono::milliseconds\{200\}\)' 'Renderer performance budget remains enforced by default outside hosted CI'
if ($release -match '(?im)^\s*Write-Host\s+\$env:WINDOWS_SIGNING_CERTIFICATE') {
  throw 'Workflow must never print signing secrets.'
}
Write-Host '[PASS] Signing secrets are not printed by the workflow'
Write-Host 'Workflow configuration checks: PASS.'
