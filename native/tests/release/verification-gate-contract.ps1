[CmdletBinding()]
param(
  [string]$Verifier = (Join-Path $PSScriptRoot '..\..\..\scripts\verify-native-release.ps1')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Verifier -PathType Leaf)) {
  throw 'Release verification gate is not implemented.'
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-gate-contract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  $artifacts = Join-Path $work 'artifacts'
  $build = Join-Path $work 'missing-build'
  New-Item -ItemType Directory -Path $artifacts | Out-Null
  $output = @(& pwsh -NoProfile -ExecutionPolicy Bypass -File $Verifier -Version '2.0.0' -Artifacts $artifacts -BuildDirectory $build 2>&1 | ForEach-Object { "$_" })
  $exitCode = $LASTEXITCODE
  if ($exitCode -eq 0) { throw 'Release verification unexpectedly passed with empty artifacts and no build.' }
  if (($output -join "`n") -notmatch '(?i)FAIL|missing|not found') {
    throw 'Release verification failed without actionable missing-evidence diagnostics.'
  }
  if (($output -join "`n") -match '(?i)release gate PASS|all checks passed') {
    throw 'Release verification must never announce success after prerequisite failures.'
  }
  Write-Output 'PASS: incomplete release inputs fail closed with actionable diagnostics.'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force
}
