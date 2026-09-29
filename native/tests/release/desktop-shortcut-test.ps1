[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Executable,
  [Parameter(Mandatory)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$Version,
  [string]$Compiler
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exePath = (Resolve-Path -LiteralPath $Executable).Path
$profileId = [guid]::NewGuid().ToString('N')
$profilesRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) 'DesktopTodoListTestProfiles'))
$profileRoot = [IO.Path]::GetFullPath((Join-Path $profilesRoot $profileId))
$dataRoot = Join-Path $profileRoot 'LocalAppData\DesktopTodoList'
$desktopRoot = Join-Path $profileRoot 'Desktop'
$installRoot = Join-Path $profileRoot 'ProgramFiles\DesktopTodoList'
$outputRoot = Join-Path $profileRoot 'InstallerOutput'
$registryName = "DesktopTodoListTest_$profileId"
$startMenuRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
$testGroupPath = Join-Path $startMenuRoot 'DesktopTodoListTest'
$oldDataRoot = $env:DESKTOP_TODO_TEST_DATA_ROOT
$oldAutostartName = $env:DESKTOP_TODO_TEST_AUTOSTART_NAME
$ownsTestGroup = $false
$completed = $false

if (Test-Path -LiteralPath $profileRoot) { throw "Refusing to overwrite an existing test profile: $profileRoot" }
if (Test-Path -LiteralPath $testGroupPath) { throw "Refusing to alter an existing Start Menu test group: $testGroupPath" }
$uninstallerKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopTodoList.Native.Test.$registryName`_is1"
if (Test-Path -LiteralPath $uninstallerKey) { throw "Refusing to alter an existing test installation: $uninstallerKey" }

try {
  New-Item -ItemType Directory -Path $profileRoot | Out-Null
  New-Item -ItemType Directory -Path $desktopRoot | Out-Null
  $builder = Join-Path $repo 'installer\build-installer.ps1'
  $setupPath = & $builder -Exe $exePath -Architecture x64 -Version $Version `
    -OutputDirectory $outputRoot -Compiler $Compiler `
    -TestDataDirectory $dataRoot -TestAutostartName $registryName `
    -TestDesktopDirectory $desktopRoot
  if ($LASTEXITCODE -ne 0) { throw "Test installer build failed with exit code $LASTEXITCODE." }
  $setupPath = [string]($setupPath | Select-Object -Last 1)
  if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) { throw 'Test installer was not created.' }

  $env:DESKTOP_TODO_TEST_DATA_ROOT = $dataRoot
  $env:DESKTOP_TODO_TEST_AUTOSTART_NAME = $registryName

  function Install-TestBuild([switch]$CreateShortcut) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/SP-', "/DIR=`"$installRoot`"", "/GROUP=DesktopTodoListTest")
    if ($CreateShortcut) { $arguments += '/TASKS=desktopicon' }
    $script:ownsTestGroup = $true
    $process = Start-Process -FilePath $setupPath -ArgumentList $arguments `
      -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Test installer exited $($process.ExitCode)." }
  }

  function Remove-TestBuild {
    $uninstaller = Join-Path $installRoot 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) {
      $process = Start-Process -FilePath $uninstaller `
        -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /REMOVEUSERDATA' `
        -Wait -PassThru -WindowStyle Hidden
      if ($process.ExitCode -ne 0) { throw "Test uninstaller exited $($process.ExitCode)." }
    }
  }

  $desktopShortcut = Join-Path $desktopRoot 'DesktopTodoList.lnk'
  Install-TestBuild
  if (Test-Path -LiteralPath $desktopShortcut) {
    throw 'The desktop shortcut was created even though the task was left unchecked.'
  }
  Write-Host '[PASS] Fresh install leaves the optional desktop shortcut unchecked by default'
  Remove-TestBuild

  Install-TestBuild -CreateShortcut
  if (-not (Test-Path -LiteralPath $desktopShortcut -PathType Leaf)) {
    throw 'Selecting the desktop shortcut task did not create the shortcut.'
  }
  $shell = New-Object -ComObject WScript.Shell
  try {
    $shortcut = $shell.CreateShortcut($desktopShortcut)
    if ([IO.Path]::GetFullPath($shortcut.TargetPath) -ne [IO.Path]::GetFullPath((Join-Path $installRoot 'DesktopTodoList.exe'))) {
      throw "Desktop shortcut points to '$($shortcut.TargetPath)' instead of the installed application."
    }
  } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
  Write-Host '[PASS] Selecting the installer task creates a desktop shortcut to the installed application'
  Remove-TestBuild
  if (Test-Path -LiteralPath $desktopShortcut) { throw 'Uninstall did not remove the isolated desktop shortcut.' }
  Write-Host '[PASS] Uninstall removes the installer-created desktop shortcut'
  $completed = $true
} finally {
  $env:DESKTOP_TODO_TEST_DATA_ROOT = $oldDataRoot
  $env:DESKTOP_TODO_TEST_AUTOSTART_NAME = $oldAutostartName
  $uninstaller = Join-Path $installRoot 'unins000.exe'
  if (Test-Path -LiteralPath $uninstaller) {
    $cleanup = Start-Process -FilePath $uninstaller `
      -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /REMOVEUSERDATA' `
      -Wait -PassThru -WindowStyle Hidden
    if ($cleanup.ExitCode -ne 0) { Write-Warning "Temporary installer cleanup returned $($cleanup.ExitCode)." }
  }
  Remove-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' `
    -Name $registryName -ErrorAction SilentlyContinue
  if ($ownsTestGroup -and (Test-Path -LiteralPath $testGroupPath)) {
    Remove-Item -LiteralPath $testGroupPath -Recurse -Force
  }
  if ([IO.Path]::GetRelativePath($profilesRoot, $profileRoot) -eq $profileId) {
    Remove-Item -LiteralPath $profileRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}
if (-not $completed) { throw 'Desktop shortcut verification did not complete.' }
Write-Host 'Desktop shortcut installer checks: PASS.'
