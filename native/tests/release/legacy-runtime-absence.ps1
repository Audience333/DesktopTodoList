[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$retiredPaths = @(
  'src',
  'host',
  '启动待办.bat',
  'tests/运行全部测试.bat',
  'tests/运行全部测试.bat',
  'tests/run-tests.js',
  'tests/test-backups.js',
  'tests/test-import-export.js',
  'tests/test-persistence.js',
  'tests/test-reminders.js',
  'tests/test-server.ps1',
  'tests/test-window.ps1'
)
$present = @($retiredPaths | Where-Object { Test-Path -LiteralPath (Join-Path $root $_) })
if ($present.Count -gt 0) {
  throw ("Legacy browser runtime or superseded tests are still active: " + ($present -join ', '))
}

Write-Output 'PASS: retired browser runtime, launcher, and obsolete tests are absent.'
