[CmdletBinding()]
param([string]$NativeBuildDirectory = 'out\build\windows-x64')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$results = New-Object System.Collections.Generic.List[object]

function Invoke-TestGroup {
  param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$File,
    [Parameter(Mandatory)][string[]]$Arguments
  )

  Write-Host ""
  Write-Host "=== $Name ===" -ForegroundColor Cyan
  & $File @Arguments
  $code = $LASTEXITCODE
  $results.Add([pscustomobject]@{ Name = $Name; ExitCode = $code })
  if ($code -ne 0) {
    Write-Host "[FAIL] $Name (exit $code)" -ForegroundColor Red
  } else {
    Write-Host "[PASS] $Name" -ForegroundColor Green
  }
}

Push-Location $root
try {
  $nativeBuildPath = if ([IO.Path]::IsPathRooted($NativeBuildDirectory)) {
    [IO.Path]::GetFullPath($NativeBuildDirectory)
  } else {
    [IO.Path]::GetFullPath((Join-Path $root $NativeBuildDirectory))
  }
  if (Test-Path -LiteralPath (Join-Path $nativeBuildPath 'CTestTestfile.cmake')) {
    Invoke-TestGroup -Name 'Native Widget Acceptance' -File 'powershell.exe' -Arguments @(
      '-NoProfile','-ExecutionPolicy','Bypass','-File','native/tests/windows/widget-acceptance.ps1',
      '-BuildDirectory',$nativeBuildPath
    )
  }
  Invoke-TestGroup -Name 'Release Documentation' -File 'pwsh.exe' -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File','native/tests/release/documentation-check.ps1')
  Invoke-TestGroup -Name 'Release Workflow Configuration' -File 'pwsh.exe' -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File','native/tests/release/workflow-config.ps1')
  Invoke-TestGroup -Name 'Legacy Runtime Retirement' -File 'pwsh.exe' -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File','native/tests/release/legacy-runtime-absence.ps1')
  Invoke-TestGroup -Name 'Release Gate Fail-Closed Contract' -File 'pwsh.exe' -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File','native/tests/release/verification-gate-contract.ps1')
} finally {
  Pop-Location
}

$failed = @($results | Where-Object ExitCode -ne 0)
Write-Host ""
Write-Host '=== Summary ===' -ForegroundColor Cyan
foreach ($result in $results) {
  $label = if ($result.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }
  Write-Host ("[{0}] {1}" -f $label, $result.Name)
}

if ($failed.Count -gt 0) { exit 1 }
exit 0
