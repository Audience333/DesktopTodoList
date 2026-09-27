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
  $ctest = Get-Command 'ctest.exe' -ErrorAction SilentlyContinue
  if (-not $ctest) {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
      $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
      if ($vsRoot) {
        $bundledCtest = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
        if (Test-Path -LiteralPath $bundledCtest) { $ctest = Get-Item -LiteralPath $bundledCtest }
      }
    }
  }
  if ($ctest -and (Test-Path -LiteralPath 'out\build\windows-x64\CTestTestfile.cmake')) {
    $ctestPath = if ($ctest.Source) { $ctest.Source } else { $ctest.FullName }
    Invoke-TestGroup -Name 'Native' -File $ctestPath -Arguments @('--preset','windows-x64-debug','--output-on-failure')
  }
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
