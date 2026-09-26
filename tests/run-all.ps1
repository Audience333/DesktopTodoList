[CmdletBinding()]
param()

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
  Invoke-TestGroup -Name 'JavaScript' -File 'node.exe' -Arguments @('tests/run-tests.js')
  Invoke-TestGroup -Name 'Server' -File 'powershell.exe' -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File','tests/test-server.ps1')
  Invoke-TestGroup -Name 'Window' -File 'powershell.exe' -Arguments @('-NoProfile','-ExecutionPolicy','Bypass','-File','tests/test-window.ps1')
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
