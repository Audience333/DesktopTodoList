[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Exe,
  [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture,
  [Parameter(Mandatory)][string]$Version,
  [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\out\packages\$Version\$Architecture"),
  [string]$Readme = (Join-Path $PSScriptRoot '..\README.md'),
  [string]$Privacy = (Join-Path $PSScriptRoot '..\docs\privacy.md'),
  [string]$License = (Join-Path $PSScriptRoot '..\LICENSE.txt')
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
  throw "Version must be semantic MAJOR.MINOR.PATCH: $Version"
}
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$readmePath = (Resolve-Path -LiteralPath $Readme).Path
$privacyPath = (Resolve-Path -LiteralPath $Privacy).Path
$licensePath = (Resolve-Path -LiteralPath $License).Path
$docsPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\docs')).Path
$guidePaths = @{}
foreach ($guide in @('install.md', 'migrate-from-web-version.md', 'troubleshooting.md')) {
  $guidePaths[$guide] = (Resolve-Path -LiteralPath (Join-Path $docsPath $guide)).Path
}
foreach ($file in @($exePath, $readmePath, $privacyPath, $licensePath) + @($guidePaths.Values)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required package input missing: $file" }
}

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
  throw ("Executable architecture '{0}' does not match requested '{1}'." -f ('0x{0:X4}' -f $machine), $Architecture)
}

foreach ($source in @($readmePath, $privacyPath, $licensePath) + @($guidePaths.Values)) {
  $text = [IO.File]::ReadAllText($source)
  if ($text -match '(?i)https?://') { throw "Offline package documentation must not contain a remote URL: $source" }
}

$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$zipPath = Join-Path $outputPath "DesktopTodoList-$Architecture-portable.zip"
$stage = Join-Path ([IO.Path]::GetTempPath()) ("DesktopTodoList-portable-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
  Copy-Item -LiteralPath $exePath -Destination (Join-Path $stage 'DesktopTodoList.exe')
  Copy-Item -LiteralPath $readmePath -Destination (Join-Path $stage 'README.md')
  Copy-Item -LiteralPath $privacyPath -Destination (Join-Path $stage 'PRIVACY.md')
  Copy-Item -LiteralPath $licensePath -Destination (Join-Path $stage 'LICENSE.txt')
  $stageDocs = Join-Path $stage 'docs'
  New-Item -ItemType Directory -Path $stageDocs | Out-Null
  foreach ($guide in $guidePaths.Keys) {
    Copy-Item -LiteralPath $guidePaths[$guide] -Destination (Join-Path $stageDocs $guide)
  }
  if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
  Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal
} finally {
  Remove-Item -LiteralPath $stage -Recurse -Force
}
Write-Output $zipPath
