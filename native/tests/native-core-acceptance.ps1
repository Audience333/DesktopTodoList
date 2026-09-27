[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$release = Join-Path $root 'out\build\windows-x64\native\Release\DesktopTodoList.exe'
$debug = Join-Path $root 'out\build\windows-x64\native\Debug\DesktopTodoList.exe'
$executable = if (Test-Path -LiteralPath $release) { $release } elseif (Test-Path -LiteralPath $debug) { $debug } else { $null }
if (-not $executable) {
  Write-Error 'DesktopTodoList.exe was not found. Build the native target first.'
}

$fixture = Join-Path $root 'native\tests\fixtures\schema-v1-export.json'
$process = Start-Process -FilePath $executable -ArgumentList @('--verify-core', ('"{0}"' -f $fixture)) -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
  Write-Error "Native core verification failed with exit code $($process.ExitCode)."
}

Write-Host "[PASS] Native core acceptance ($([IO.Path]::GetFileName((Split-Path -Parent $executable))))"
exit 0
