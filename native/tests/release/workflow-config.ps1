$ErrorActionPreference = 'Stop'
$ciPath = Join-Path $PSScriptRoot '..\..\..\.github\workflows\native-ci.yml'
$releasePath = Join-Path $PSScriptRoot '..\..\..\.github\workflows\native-release.yml'
$ci = [IO.File]::ReadAllText((Resolve-Path $ciPath).Path)
$release = [IO.File]::ReadAllText((Resolve-Path $releasePath).Path)

function Assert-Pattern([string]$Text, [string]$Pattern, [string]$Description) {
  if ($Text -notmatch $Pattern) { throw "Workflow configuration is missing: $Description" }
  Write-Host "[PASS] $Description"
}

Assert-Pattern $ci '(?m)^  pull_request:' 'PRs run native CI'
Assert-Pattern $ci '(?m)^            platform: ARM64$' 'CI cross-builds ARM64'
Assert-Pattern $ci 'ctest --test-dir' 'CI runs the native test suite'
Assert-Pattern $ci 'SkipExecutableRun' 'CI skips execution only for ARM64 package checks'
Assert-Pattern $ci 'contents: read' 'CI token is read-only'

Assert-Pattern $release "tags:\s*\r?\n\s*- 'v\*'" 'Release runs only for pushed version tags'
Assert-Pattern $release 'RELEASE_VERSION[\s\S]*?GITHUB_ENV' 'Tag version is passed to CMake and packaging'
Assert-Pattern $release 'WINDOWS_SIGNING_CERTIFICATE_BASE64' 'Optional signing certificate is supplied through a secret'
Assert-Pattern $release 'WINDOWS_SIGNING_CERTIFICATE_PASSWORD' 'Optional signing password is supplied through a secret'
Assert-Pattern $release 'actions/download-artifact@[0-9a-f]{40}[\s\S]*?merge-multiple: true' 'Pinned architecture artifacts are merged for aggregate verification'
Assert-Pattern $release 'persist-credentials: false' 'Release checkout does not retain write-capable credentials'
Assert-Pattern $release 'release-manifest\.ps1' 'Publishing is gated by the complete release manifest'
Assert-Pattern $release 'release-manifest\.ps1[\s\S]*?ghArgs[\s\S]*?release.*create' 'GitHub Release creation follows manifest validation'
Assert-Pattern $release 'contents: write' 'Only the publish job receives release-write permission'
if ($release -match '(?im)^\s*Write-Host\s+\$env:WINDOWS_SIGNING_CERTIFICATE') {
  throw 'Workflow must never print signing secrets.'
}
Write-Host '[PASS] Signing secrets are not printed by the workflow'
Write-Host 'Workflow configuration checks: PASS.'
