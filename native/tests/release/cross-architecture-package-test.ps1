[CmdletBinding()]
param([Parameter(Mandatory)][string]$X64Executable)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-arm64-package-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $X64Executable).Path)
  $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
  if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw 'Source executable has an invalid PE signature.' }
  [Array]::Copy([BitConverter]::GetBytes([UInt16]0xaa64), 0, $bytes, $peOffset + 4, 2)
  $armExe = Join-Path $work 'DesktopTodoList-arm64-fixture.exe'
  [IO.File]::WriteAllBytes($armExe, $bytes)

  $packageDirectory = Join-Path $work 'package'
  & (Join-Path $repo 'scripts\package-portable.ps1') -Exe $armExe -Architecture arm64 -Version '2.0.0' -OutputDirectory $packageDirectory | Out-Null
  $zip = Join-Path $packageDirectory 'DesktopTodoList-arm64-portable.zip'
  & (Join-Path $repo 'native\tests\release\package-contents.ps1') `
    -Package $zip -Architecture arm64 -Version '2.0.0' -SkipExecutableRun
  Write-Host '[PASS] Cross-architecture packaging validates metadata without launching the foreign executable'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
