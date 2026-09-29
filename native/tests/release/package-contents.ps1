[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Package,
  [Parameter(Mandatory)][string]$Architecture,
  [Parameter(Mandatory)][string]$Version,
  [string]$Installer,
  [switch]$SkipExecutableRun,
  [string]$InstallerScript = (Join-Path $PSScriptRoot '..\..\..\installer\DesktopTodoList.iss')
)

$ErrorActionPreference = 'Stop'
if ($Architecture -notin @('x64', 'arm64')) { throw "Unsupported architecture: $Architecture" }
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ("DesktopTodoList-package-check-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  Expand-Archive -LiteralPath $packagePath -DestinationPath $work
  $files = @(Get-ChildItem -LiteralPath $work -File -Recurse)
  $names = @($files | ForEach-Object { $_.Name })
  foreach ($required in @('DesktopTodoList.exe', 'README.md', 'PRIVACY.md', 'LICENSE.txt')) {
    if ($names -notcontains $required) { throw "Package is missing required file: $required" }
    Write-Host "[PASS] Required file: $required"
  }

  $exe = Join-Path $work 'DesktopTodoList.exe'
  if (-not (Test-Path -LiteralPath $exe)) { throw 'DesktopTodoList.exe must be at the package root.' }
  $stream = [IO.File]::OpenRead($exe)
  try {
    $reader = [IO.BinaryReader]::new($stream)
    $stream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    $stream.Position = $peOffset
    $signature = $reader.ReadUInt32()
    $machine = $reader.ReadUInt16()
  } finally { $stream.Dispose() }
  $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0xaa64 }
  if ($signature -ne 0x00004550 -or $machine -ne $expectedMachine) {
    throw ("Executable architecture mismatch: expected {0}, got 0x{1:X4}" -f $Architecture, $machine)
  }
  Write-Host '[PASS] Executable PE architecture matches package'

  $metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
  if ($metadata.ProductVersion.Trim() -cne $Version) {
    throw "Portable executable version mismatch: $($metadata.ProductVersion)"
  }
  if ($SkipExecutableRun) {
    Write-Host '[SKIP] Executable launch skipped for cross-architecture package verification'
  } else {
    $versionOutput = & $exe --version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $versionOutput.Trim() -ne "DesktopTodoList $Version") {
      throw "Portable executable runtime version mismatch: $versionOutput"
    }
    Write-Host '[PASS] Extracted executable runs without installation and reports the requested version'
  }

  $forbidden = @($files | Where-Object {
    $_.Extension -in @('.html', '.htm', '.js', '.ps1', '.pdb', '.bat', '.cmd')
  })
  if ($forbidden.Count -gt 0) { throw "Forbidden runtime/package file: $($forbidden[0].FullName)" }
  Write-Host '[PASS] No legacy web runtime, PowerShell, debug symbols, or launch scripts'

  foreach ($file in $files) {
    if ($file.Extension -in @('.md', '.txt', '.json', '.ini')) {
      $content = [IO.File]::ReadAllText($file.FullName)
      if ($content -match '(?i)https?://') { throw "Remote URL found in package file: $($file.Name)" }
      if ($content -match '(?i)requireAdmin|PrivilegesRequired\s*=\s*admin|runas') {
        throw "Unexpected elevation configuration found in package file: $($file.Name)"
      }
    }
  }
  $binaryText = [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($exe))
  $embeddedUrls = @([regex]::Matches($binaryText, '(?i)https?://[^\s"''<>]+') | ForEach-Object { $_.Value })
  $manifestNamespaces = @(
    'http://schemas.microsoft.com/SMI/2005/WindowsSettings',
    'http://schemas.microsoft.com/SMI/2016/WindowsSettings'
  )
  $nonManifestUrls = @($embeddedUrls | Where-Object { $_ -notin $manifestNamespaces })
  if ($nonManifestUrls.Count -gt 0) { throw "Unexpected remote URL embedded in executable: $($nonManifestUrls[0])" }
  Write-Host '[PASS] No remote URL configuration; embedded URL text is limited to the Windows manifest namespace'

  $installerScriptContent = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $InstallerScript).Path)
  $languageFile = Join-Path (Split-Path -Parent $InstallerScript) 'messages.zh-CN.isl'
  $languageMessages = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $languageFile).Path)
  foreach ($pattern in @(
    '(?m)^PrivilegesRequired\s*=\s*lowest\s*$',
    '(?m)^DefaultDirName=\{localappdata\}\\Programs\\DesktopTodoList\s*$',
    'x64compatible and not arm64',
    '(?m)^\s*#define AllowedArchitectures "arm64"\s*$',
    'AppUserModelID:\s*"DesktopTodoList\.Native"',
    '\{cm:UninstallProgram,DesktopTodoList\}'
  )) {
    if ($installerScriptContent -notmatch $pattern) { throw "Installer script is missing required per-user/architecture configuration: $pattern" }
  }
  foreach ($pattern in @('UninstallProgram=卸载 %1', 'LaunchProgram=启动 %1')) {
    if ($languageMessages -notmatch $pattern) { throw "Installer language file is missing required localized text: $pattern" }
  }
  if ($installerScriptContent -match '(?i)HKCU.*CurrentVersion\\Run|CurrentVersion\\Run.*HKCU') {
    throw 'Installer must not enable autostart before the user opts in.'
  }
  $notificationPath = Join-Path $PSScriptRoot '..\..\platform\windows\notification_service.cpp'
  $notificationSource = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $notificationPath).Path)
  if ($notificationSource -notmatch 'SetCurrentProcessExplicitAppUserModelID\(L"DesktopTodoList\.Native"\)') {
    throw 'Application notification identity must match the installer shortcut AppUserModelID.'
  }
  Write-Host '[PASS] Per-user installer, architecture restrictions, toast shortcut, and no installer autostart'
  Write-Host '[PASS] Application and installer shortcut use the same AppUserModelID'

  if ($Installer) {
    $installerPath = (Resolve-Path -LiteralPath $Installer).Path
    if ((Get-Item -LiteralPath $installerPath).Length -le 0) { throw 'Installer artifact is empty.' }
    $installerVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($installerPath)
    $expectedInstallerName = "DesktopTodoList-$Architecture-Setup.exe"
    if ($installerVersion.ProductVersion.Trim() -ne $Version -or $installerVersion.OriginalFilename.Trim() -ne $expectedInstallerName) {
      throw "Installer metadata does not match package identity/version (ProductVersion='$($installerVersion.ProductVersion.Trim())', OriginalFilename='$($installerVersion.OriginalFilename.Trim())')."
    }
    Write-Host '[PASS] Installer artifact has matching filename and version metadata'
  }
  Write-Host 'Package content checks: 9 passed.'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force
}
