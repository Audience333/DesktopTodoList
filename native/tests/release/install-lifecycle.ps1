[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$PreviousInstaller,
  [Parameter(Mandatory)][string]$CurrentInstaller,
  [Parameter(Mandatory)][string]$ProfileRoot
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$installScript = [IO.File]::ReadAllText((Join-Path $repo 'installer\DesktopTodoList.iss'))
$buildScript = [IO.File]::ReadAllText((Join-Path $repo 'installer\build-installer.ps1'))
$application = [IO.File]::ReadAllText((Join-Path $repo 'native\app\application.cpp'))
$entryPoint = [IO.File]::ReadAllText((Join-Path $repo 'native\app\main.cpp'))
$buildConfig = [IO.File]::ReadAllText((Join-Path $repo 'native\CMakeLists.txt'))

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Name) {
  if ($Text -notmatch $Pattern) { throw "[FAIL] $Name" }
  Write-Host "[PASS] $Name"
}

Assert-Contains $buildConfig 'DESKTOP_TODO_TEST_PROFILE' 'A test-profile build option exists'
Assert-Contains $application 'DESKTOP_TODO_TEST_DATA_ROOT' 'Test builds can isolate application data'
Assert-Contains $application '--test-profile-sync' 'Test builds can run the startup autostart sync without opening a widget'
Assert-Contains $installScript 'TestDataDirectory' 'Installer can target a test-only profile directory'
Assert-Contains $installScript '/REMOVEUSERDATA' 'Uninstaller has an explicit testable data-removal choice'
Assert-Contains $installScript 'DelTree' 'Data deletion is scoped to an exact directory'
Assert-Contains $installScript 'PrivilegesRequired=lowest' 'Install lifecycle does not request elevation'
Assert-Contains $installScript 'DesktopTodoList.Native' 'Start Menu shortcut carries the notification identity'
Assert-Contains $buildScript 'TestDataDirectory' 'Installer build script forwards a test profile path'

$profilesRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) 'DesktopTodoListTestProfiles'))
$profilePath = [IO.Path]::GetFullPath($ProfileRoot)
$relativeProfile = [IO.Path]::GetRelativePath($profilesRoot, $profilePath)
$profileGuid = [guid]::Empty
if ($relativeProfile.StartsWith('..') -or [IO.Path]::IsPathRooted($relativeProfile) -or
    $relativeProfile.Contains([IO.Path]::DirectorySeparatorChar) -or
    -not [guid]::TryParse($relativeProfile, [ref]$profileGuid)) {
  throw "ProfileRoot must be a new GUID directory under $profilesRoot"
}
if (Test-Path -LiteralPath $profilePath) { throw "Refusing to overwrite an existing test profile: $profilePath" }
foreach ($installerPath in @($PreviousInstaller, $CurrentInstaller)) {
  if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) { throw "Installer does not exist: $installerPath" }
}

$previousPath = (Resolve-Path -LiteralPath $PreviousInstaller).Path
$currentPath = (Resolve-Path -LiteralPath $CurrentInstaller).Path
$previousVersion = [version]([Diagnostics.FileVersionInfo]::GetVersionInfo($previousPath).ProductVersion.Trim())
$currentVersion = [version]([Diagnostics.FileVersionInfo]::GetVersionInfo($currentPath).ProductVersion.Trim())
if ($previousVersion -ge $currentVersion) { throw "Previous installer $previousVersion must be older than current installer $currentVersion." }
$testProfileScript = Join-Path $PSScriptRoot 'test-profile.ps1'
$profile = & $testProfileScript -Root $profilePath -Mode Seed
$installRoot = Join-Path $profilePath 'ProgramFiles\DesktopTodoList'
$programsFolder = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
$groupPath = Join-Path $programsFolder 'DesktopTodoListTest'
$shortcutPath = Join-Path $groupPath 'DesktopTodoList.lnk'
$registryPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$appDataBefore = Get-FileHash -LiteralPath (Join-Path $profile.DataDirectory 'data.json') -Algorithm SHA256
$backupBefore = Get-FileHash -LiteralPath (Join-Path $profile.DataDirectory 'backups\seeded.json') -Algorithm SHA256
$oldDataRoot = $env:DESKTOP_TODO_TEST_DATA_ROOT
$oldAutostartName = $env:DESKTOP_TODO_TEST_AUTOSTART_NAME
$didPass = $false

function Invoke-SilentInstall([string]$InstallerPath, [string]$InstallDirectory) {
  $arguments = "/VERYSILENT /SUPPRESSMSGBOXES /SP- /DIR=`"$InstallDirectory`""
  $process = Start-Process -FilePath $InstallerPath -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
  if ($process.ExitCode -ne 0) { throw "Installer failed with exit code $($process.ExitCode): $InstallerPath" }
}

function Invoke-ProfileSync([string]$Executable) {
  $process = Start-Process -FilePath $Executable -ArgumentList '--test-profile-sync' -Wait -PassThru -WindowStyle Hidden
  if ($process.ExitCode -ne 0) { throw "Test-profile startup sync failed with exit code $($process.ExitCode)." }
}

function Assert-ShortcutTarget([string]$ExpectedTarget) {
  if (-not (Test-Path -LiteralPath $shortcutPath -PathType Leaf)) { throw "Start Menu shortcut missing: $shortcutPath" }
  $shell = New-Object -ComObject WScript.Shell
  try {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    if ([IO.Path]::GetFullPath($shortcut.TargetPath) -ne [IO.Path]::GetFullPath($ExpectedTarget)) {
      throw "Start Menu shortcut points to '$($shortcut.TargetPath)', expected '$ExpectedTarget'."
    }
  } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
}

try {
  $env:DESKTOP_TODO_TEST_DATA_ROOT = $profile.DataDirectory
  $env:DESKTOP_TODO_TEST_AUTOSTART_NAME = $profile.RegistryName

  Invoke-SilentInstall $previousPath $installRoot
  $installedExe = Join-Path $installRoot 'DesktopTodoList.exe'
  if (-not (Test-Path -LiteralPath $installedExe)) { throw 'Clean install did not place DesktopTodoList.exe in the isolated install root.' }
  Assert-ShortcutTarget $installedExe
  & $testProfileScript -Root $profilePath -Mode AssertPreserved

  Invoke-ProfileSync $installedExe
  $runCommand = (Get-ItemProperty -LiteralPath $registryPath -Name $profile.RegistryName).($profile.RegistryName)
  if ($runCommand -ne ('"' + $installedExe + '"')) { throw "Autostart command was not synchronized to the installed path: $runCommand" }
  Write-Host '[PASS] Enabled autostart command targets the installed executable'

  Set-ItemProperty -LiteralPath $registryPath -Name $profile.RegistryName -Value ('"' + (Join-Path $profilePath 'Obsolete\DesktopTodoList.exe') + '"')
  Invoke-SilentInstall $currentPath $installRoot
  $installedVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($installedExe).ProductVersion.Trim()
  if ($installedVersion -ne $currentVersion.ToString()) { throw "Upgrade left version $installedVersion instead of $currentVersion." }
  Assert-ShortcutTarget $installedExe
  $dataAfterUpgrade = Get-FileHash -LiteralPath (Join-Path $profile.DataDirectory 'data.json') -Algorithm SHA256
  $backupAfterUpgrade = Get-FileHash -LiteralPath (Join-Path $profile.DataDirectory 'backups\seeded.json') -Algorithm SHA256
  if ($dataAfterUpgrade.Hash -ne $appDataBefore.Hash -or $backupAfterUpgrade.Hash -ne $backupBefore.Hash) {
    throw 'In-place upgrade modified seeded user data or backup.'
  }
  Write-Host '[PASS] In-place upgrade preserves data.json and backup byte-for-byte'

  Invoke-ProfileSync $installedExe
  $runCommand = (Get-ItemProperty -LiteralPath $registryPath -Name $profile.RegistryName).($profile.RegistryName)
  if ($runCommand -ne ('"' + $installedExe + '"')) { throw "Upgrade did not update the autostart command: $runCommand" }
  Write-Host '[PASS] Upgrade refreshes the autostart command to the current installation path'

  $uninstaller = Join-Path $installRoot 'unins000.exe'
  $removed = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES' -Wait -PassThru -WindowStyle Hidden
  if ($removed.ExitCode -ne 0) { throw "Default uninstall failed with exit code $($removed.ExitCode)." }
  if (Test-Path -LiteralPath $installRoot) { throw 'Default uninstall left the isolated program directory.' }
  & $testProfileScript -Root $profilePath -Mode AssertPreserved
  $remainingRunValue = (Get-ItemProperty -LiteralPath $registryPath -ErrorAction SilentlyContinue).PSObject.Properties[$profile.RegistryName]
  if ($null -ne $remainingRunValue) {
    throw 'Default uninstall left the test autostart registry value behind.'
  }
  Write-Host '[PASS] Default uninstall removes the program/autostart entry but preserves personal data'

  Invoke-SilentInstall $currentPath $installRoot
  $uninstaller = Join-Path $installRoot 'unins000.exe'
  $removed = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /REMOVEUSERDATA' -Wait -PassThru -WindowStyle Hidden
  if ($removed.ExitCode -ne 0) { throw "Opt-in data-removal uninstall failed with exit code $($removed.ExitCode)." }
  & $testProfileScript -Root $profilePath -Mode AssertRemoved
  Write-Host '[PASS] Explicit data-removal option removes only the isolated application data directory'
  $didPass = $true
} finally {
  $env:DESKTOP_TODO_TEST_DATA_ROOT = $oldDataRoot
  $env:DESKTOP_TODO_TEST_AUTOSTART_NAME = $oldAutostartName
  $remainingUninstaller = Join-Path $installRoot 'unins000.exe'
  if (Test-Path -LiteralPath $remainingUninstaller) {
    $cleanup = Start-Process -FilePath $remainingUninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /REMOVEUSERDATA' -Wait -PassThru -WindowStyle Hidden
    if ($cleanup.ExitCode -ne 0) { Write-Warning "Temporary installer cleanup returned $($cleanup.ExitCode)." }
  }
  Remove-ItemProperty -LiteralPath $registryPath -Name $profile.RegistryName -ErrorAction SilentlyContinue
  $safeProfileParent = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) 'DesktopTodoListTestProfiles'))
  if ([IO.Path]::GetRelativePath($safeProfileParent, $profilePath) -eq $profileGuid.ToString('N')) {
    Remove-Item -LiteralPath $profilePath -Recurse -Force -ErrorAction SilentlyContinue
  }
  if ((Test-Path -LiteralPath $groupPath) -and
      [IO.Path]::GetFullPath($groupPath).StartsWith([IO.Path]::GetFullPath((Join-Path $programsFolder 'DesktopTodoListTest')), [StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $groupPath -Recurse -Force -ErrorAction SilentlyContinue
  }
}
if (-not $didPass) { throw 'Lifecycle verification did not complete.' }
Write-Host 'Installer lifecycle checks: PASS.'
