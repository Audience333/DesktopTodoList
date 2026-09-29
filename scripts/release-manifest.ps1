[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$ArtifactsDirectory,
  [Parameter(Mandatory)][string]$Version,
  [Parameter(Mandatory)][string]$Tag
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
  throw "Version must be semantic MAJOR.MINOR.PATCH: $Version"
}
if ($Tag -cne "v$Version") { throw "Tag '$Tag' must match version '$Version' exactly as v$Version." }

$root = (Resolve-Path -LiteralPath $ArtifactsDirectory).Path
$expectedNames = @(
  'DesktopTodoList-x64.exe',
  'DesktopTodoList-arm64.exe',
  'DesktopTodoList-x64-Setup.exe',
  'DesktopTodoList-arm64-Setup.exe',
  'DesktopTodoList-x64-portable.zip',
  'DesktopTodoList-arm64-portable.zip',
  'checksums.txt'
) | Sort-Object
$actualFiles = @(Get-ChildItem -LiteralPath $root -File -Force | ForEach-Object Name | Sort-Object)
if (Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualFiles) {
  throw "Release directory must contain exactly: $($expectedNames -join ', ')"
}

$checksumPath = Join-Path $root 'checksums.txt'
$checksumLines = [IO.File]::ReadAllLines($checksumPath)
if ($checksumLines.Count -ne 7 -or $checksumLines[0] -notmatch '^# Signature status: (valid|unsigned)$') {
  throw 'checksums.txt must declare signature status and contain exactly six artifact hashes.'
}
$signatureStatus = $Matches[1]
if ($signatureStatus -ne 'valid') {
  throw 'Release artifacts must be Authenticode-signed; unsigned manifests are rejected.'
}
$listedHashes = @{}
foreach ($line in $checksumLines | Select-Object -Skip 1) {
  if ($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$') { throw "Invalid checksum line: $line" }
  $name = $Matches[2]
  if ($listedHashes.ContainsKey($name)) { throw "Duplicate checksum entry: $name" }
  $listedHashes[$name] = $Matches[1].ToLowerInvariant()
}
$payloadNames = @($expectedNames | Where-Object { $_ -ne 'checksums.txt' })
if (Compare-Object -ReferenceObject ($payloadNames | Sort-Object) -DifferenceObject (@($listedHashes.Keys) | Sort-Object)) {
  throw 'checksums.txt must contain exactly one hash for each release artifact.'
}
foreach ($name in $payloadNames) {
  $actualHash = (Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actualHash -cne $listedHashes[$name]) { throw "SHA-256 mismatch for $name." }
}

function Get-PeMachine([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  try {
    $reader = [IO.BinaryReader]::new($stream)
    if ($stream.Length -lt 256) { throw "PE file is too small: $Path" }
    $stream.Position = 0
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Missing MZ signature: $Path" }
    $stream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    if ($peOffset -lt 64 -or $peOffset + 6 -gt $stream.Length) { throw "Invalid PE header offset: $Path" }
    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw "Missing PE signature: $Path" }
    return $reader.ReadUInt16()
  } finally { $stream.Dispose() }
}

function Assert-VersionAndArchitecture([string]$Path, [string]$Architecture, [string]$Description) {
  $expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0xaa64 }
  $machine = Get-PeMachine $Path
  if ($machine -ne $expectedMachine) {
    throw "$Description has machine 0x$('{0:X4}' -f $machine), expected $Architecture."
  }
  $fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
  if ($fileVersion.ProductVersion.Trim() -cne $Version) {
    throw "$Description product version '$($fileVersion.ProductVersion)' does not match $Version."
  }
  return $fileVersion
}

function Assert-Signature([string]$Path, [string]$Description) {
  $status = (Get-AuthenticodeSignature -LiteralPath $Path).Status.ToString()
  if ($signatureStatus -eq 'valid' -and $status -ne 'Valid') {
    throw "$Description must have a valid Authenticode signature; actual status is $status."
  }
  if ($signatureStatus -eq 'unsigned' -and $status -ne 'NotSigned') {
    throw "$Description is recorded as unsigned, but actual signature status is $status."
  }
}

$work = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-release-manifest-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
  foreach ($architecture in @('x64', 'arm64')) {
    $executableName = "DesktopTodoList-$architecture.exe"
    $installerName = "DesktopTodoList-$architecture-Setup.exe"
    $portableName = "DesktopTodoList-$architecture-portable.zip"

    $executablePath = Join-Path $root $executableName
    $exeInfo = Assert-VersionAndArchitecture $executablePath $architecture 'Application executable'
    if ($exeInfo.OriginalFilename.Trim() -cne 'DesktopTodoList.exe') {
      throw "$executableName has unexpected original filename '$($exeInfo.OriginalFilename)'."
    }
    Assert-Signature $executablePath $executableName

    $installerPath = Join-Path $root $installerName
    $installerMachine = Get-PeMachine $installerPath
    if ($installerMachine -ne 0x014c) {
      throw "$installerName has machine 0x$('{0:X4}' -f $installerMachine); the Inno Setup bootstrapper must be x86."
    }
    $installerInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($installerPath)
    if ($installerInfo.ProductVersion.Trim() -cne $Version) {
      throw "$installerName product version '$($installerInfo.ProductVersion)' does not match $Version."
    }
    if ($installerInfo.OriginalFilename.Trim() -cne $installerName) {
      throw "$installerName has unexpected original filename '$($installerInfo.OriginalFilename)'."
    }
    Assert-Signature $installerPath $installerName

    $unpacked = Join-Path $work $architecture
    Expand-Archive -LiteralPath (Join-Path $root $portableName) -DestinationPath $unpacked
    $portableFiles = @(Get-ChildItem -LiteralPath $unpacked -File -Recurse | ForEach-Object {
      [IO.Path]::GetRelativePath($unpacked, $_.FullName).Replace('\', '/')
    } | Sort-Object)
    $expectedPortableFiles = @(
      'DesktopTodoList.exe', 'LICENSE.txt', 'PRIVACY.md', 'README.md',
      'docs/install.md', 'docs/migrate-from-web-version.md', 'docs/troubleshooting.md'
    ) | Sort-Object
    if (Compare-Object -ReferenceObject $expectedPortableFiles -DifferenceObject $portableFiles) {
      throw "$portableName must contain exactly the executable, three root documents, and three version-independent user guides."
    }
    $portableExe = Join-Path $unpacked 'DesktopTodoList.exe'
    $portableInfo = Assert-VersionAndArchitecture $portableExe $architecture 'Portable executable'
    if ($portableInfo.OriginalFilename.Trim() -cne 'DesktopTodoList.exe') {
      throw "$portableName contains an executable with an unexpected original filename."
    }
    Assert-Signature $portableExe "$portableName executable"
    foreach ($document in @('README.md', 'PRIVACY.md', 'LICENSE.txt')) {
      if ((Get-Item -LiteralPath (Join-Path $unpacked $document)).Length -le 0) {
        throw "$portableName contains an empty $document."
      }
    }
  }
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Release manifest PASS: $Tag, signature status $signatureStatus, six architecture-specific artifacts, exact versions, and SHA-256 checksums."
