[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$X64Executable,
  [Parameter(Mandatory)][string]$Arm64Executable,
  [Parameter(Mandatory)][string]$X64Installer,
  [Parameter(Mandatory)][string]$Arm64Installer,
  [Parameter(Mandatory)][string]$Version,
  [Parameter(Mandatory)][string]$Tag
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$manifestScript = Join-Path $PSScriptRoot '..\..\..\scripts\release-manifest.ps1'
if (-not (Test-Path -LiteralPath $manifestScript -PathType Leaf)) {
  throw "Release manifest implementation does not exist yet: $manifestScript"
}
$work = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-manifest-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

function New-Fixture([string]$Directory, [bool]$ExtraFile = $false) {
  New-Item -ItemType Directory -Path $Directory | Out-Null
  foreach ($architecture in @('x64', 'arm64')) {
    $exeName = "DesktopTodoList-$architecture.exe"
    $installerName = "DesktopTodoList-$architecture-Setup.exe"
    $portableName = "DesktopTodoList-$architecture-portable.zip"
    $exeSource = if ($architecture -eq 'x64') { $X64Executable }
      else { $Arm64Executable }
    $installerSource = if ($architecture -eq 'x64') { $X64Installer } else { $Arm64Installer }
    Copy-Item -LiteralPath $exeSource -Destination (Join-Path $Directory $exeName)
    Copy-Item -LiteralPath $installerSource -Destination (Join-Path $Directory $installerName)
    $stage = Join-Path $Directory "$architecture-portable"
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath (Join-Path $Directory $exeName) -Destination (Join-Path $stage 'DesktopTodoList.exe')
    foreach ($doc in @('README.md', 'PRIVACY.md', 'LICENSE.txt')) {
      [IO.File]::WriteAllText((Join-Path $stage $doc), "test fixture`n", [Text.UTF8Encoding]::new($false))
    }
    $stageDocs = Join-Path $stage 'docs'
    New-Item -ItemType Directory -Path $stageDocs | Out-Null
    foreach ($doc in @('install.md', 'migrate-from-web-version.md', 'troubleshooting.md')) {
      Copy-Item -LiteralPath (Join-Path $repo "docs\$doc") -Destination (Join-Path $stageDocs $doc)
    }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $Directory $portableName)
    Remove-Item -LiteralPath $stage -Recurse -Force
  }
  & (Join-Path $repo 'scripts\write-checksums.ps1') -ArtifactsDirectory $Directory -SignatureStatus valid | Out-Null
  if ($ExtraFile) { [IO.File]::WriteAllText((Join-Path $Directory 'unexpected.txt'), 'not public') }
}

function Assert-Rejected([string]$Directory, [string]$Scenario) {
  $rejected = $false
  try { & $manifestScript -ArtifactsDirectory $Directory -Version $Version -Tag $Tag | Out-Null }
  catch { $rejected = $true }
  if (-not $rejected) { throw "Manifest incorrectly accepted $Scenario." }
  Write-Host "[PASS] Manifest rejects $Scenario"
}

try {
  $valid = Join-Path $work 'valid'
  New-Fixture $valid
  & $manifestScript -ArtifactsDirectory $valid -Version $Version -Tag $Tag | Out-Null
  Write-Host '[PASS] Complete signed x64/ARM64 release manifest'

  $unsigned = Join-Path $work 'unsigned-status'
  Copy-Item -LiteralPath $valid -Destination $unsigned -Recurse
  $unsignedChecksums = Join-Path $unsigned 'checksums.txt'
  $checksumLines = [IO.File]::ReadAllLines($unsignedChecksums)
  $checksumLines[0] = '# Signature status: unsigned'
  [IO.File]::WriteAllLines($unsignedChecksums, $checksumLines, [Text.UTF8Encoding]::new($false))
  Assert-Rejected $unsigned 'an unsigned signature status'

  $missing = Join-Path $work 'missing'
  Copy-Item -LiteralPath $valid -Destination $missing -Recurse
  Remove-Item -LiteralPath (Join-Path $missing 'DesktopTodoList-arm64-portable.zip') -Force
  Assert-Rejected $missing 'a missing ARM64 portable package'

  $mismatch = Join-Path $work 'mismatch'
  Copy-Item -LiteralPath $valid -Destination $mismatch -Recurse
  $rejected = $false
  $mismatchedVersion = if ($Version -eq '0.0.0') { '0.0.1' } else { '0.0.0' }
  try { & $manifestScript -ArtifactsDirectory $mismatch -Version $mismatchedVersion `
      -Tag "v$mismatchedVersion" | Out-Null }
  catch { $rejected = $true }
  if (-not $rejected) { throw 'Manifest incorrectly accepted artifacts whose version differs from the tag.' }
  Write-Host '[PASS] Manifest rejects an executable whose version differs from the tag'

  $extra = Join-Path $work 'extra'
  New-Fixture $extra -ExtraFile $true
  Assert-Rejected $extra 'an unexpected public artifact'

  $tampered = Join-Path $work 'tampered'
  Copy-Item -LiteralPath $valid -Destination $tampered -Recurse
  [IO.File]::AppendAllText((Join-Path $tampered 'DesktopTodoList-x64.exe'), 'tampered')
  Assert-Rejected $tampered 'an artifact with a stale SHA-256 checksum'
  Write-Host 'Release manifest tests: PASS.'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
