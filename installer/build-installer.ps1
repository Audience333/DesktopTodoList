[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Exe,
  [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture,
  [Parameter(Mandatory)][string]$Version,
  [string]$Compiler,
  [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\out\packages\$Version\$Architecture"),
  [string]$Readme = (Join-Path $PSScriptRoot '..\README.md'),
  [string]$Privacy = (Join-Path $PSScriptRoot '..\docs\privacy.md'),
  [string]$License = (Join-Path $PSScriptRoot '..\LICENSE.txt')
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
  throw "Version must be semantic MAJOR.MINOR.PATCH: $Version"
}
if (-not $Compiler) {
  $candidate = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($candidate) { $Compiler = $candidate.Source }
  else {
    $knownPaths = @(
      (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
      (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
      (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
      (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
      (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
      (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $Compiler = $knownPaths | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  }
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
  throw 'Inno Setup Compiler not found. Install Inno Setup 6.3+ or pass -Compiler explicitly.'
}
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$readmePath = (Resolve-Path -LiteralPath $Readme).Path
$privacyPath = (Resolve-Path -LiteralPath $Privacy).Path
$licensePath = (Resolve-Path -LiteralPath $License).Path
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($exePath)
if ($metadata.ProductVersion -ne $Version) {
  throw "Executable version '$($metadata.ProductVersion)' does not match requested '$Version'."
}
$stream = [IO.File]::OpenRead($exePath)
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
  throw "Executable architecture does not match installer architecture '$Architecture'."
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$scriptPath = Join-Path $PSScriptRoot 'DesktopTodoList.iss'
$iconPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\native\resources\app.ico')).Path
$messagesPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'messages.zh-CN.isl')).Path

$values = @{
  '@AppVersion@' = $Version
  '@Architecture@' = $Architecture
  '@ExePath@' = $exePath
  '@ReadmePath@' = $readmePath
  '@PrivacyPath@' = $privacyPath
  '@LicensePath@' = $licensePath
  '@OutputDirectory@' = $outputPath
  '@IconFilePath@' = $iconPath
  '@MessagesFilePath@' = $messagesPath
}
$rendered = [IO.File]::ReadAllText($scriptPath)
foreach ($token in $values.Keys) {
  $value = $values[$token].Replace('\', '/')
  if ($value.Contains('"') -or $value.Contains("`r") -or $value.Contains("`n")) {
    throw "Unsafe text in installer input for $token."
  }
  $rendered = $rendered.Replace($token, $value)
}
if ($rendered -match '@[A-Za-z]+@') { throw 'Installer template contains an unresolved input token.' }
$generatedDirectory = Join-Path ([IO.Path]::GetTempPath()) ("DesktopTodoList-installer-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $generatedDirectory | Out-Null
$generatedScript = Join-Path $generatedDirectory 'DesktopTodoList.iss'
try {
  [IO.File]::WriteAllText($generatedScript, $rendered, [Text.UTF8Encoding]::new($false))
  & $Compiler $generatedScript
  if ($LASTEXITCODE -ne 0) { throw "Inno Setup compiler failed with exit code $LASTEXITCODE." }
} finally {
  Remove-Item -LiteralPath $generatedDirectory -Recurse -Force
}
$setupPath = Join-Path $outputPath "DesktopTodoList-$Architecture-Setup.exe"
if (-not (Test-Path -LiteralPath $setupPath -PathType Leaf)) { throw "Installer output was not created: $setupPath" }
Write-Output $setupPath
