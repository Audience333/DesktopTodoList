[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$ArtifactsDirectory,
  [Parameter(Mandatory)][ValidateSet('valid', 'unsigned')][string]$SignatureStatus
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ArtifactsDirectory).Path
$expectedNames = @(
  'DesktopTodoList-x64.exe',
  'DesktopTodoList-arm64.exe',
  'DesktopTodoList-x64-Setup.exe',
  'DesktopTodoList-arm64-Setup.exe',
  'DesktopTodoList-x64-portable.zip',
  'DesktopTodoList-arm64-portable.zip'
) | Sort-Object
$actualNames = @(Get-ChildItem -LiteralPath $root -File -Force | ForEach-Object Name | Where-Object { $_ -ne 'checksums.txt' } | Sort-Object)
if (Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames) {
  throw "Cannot write checksums; expected exactly six release artifacts: $($expectedNames -join ', ')"
}

$signatureFiles = @(
  'DesktopTodoList-x64.exe', 'DesktopTodoList-arm64.exe',
  'DesktopTodoList-x64-Setup.exe', 'DesktopTodoList-arm64-Setup.exe'
)
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-checksum-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
  foreach ($architecture in @('x64', 'arm64')) {
    $portableRoot = Join-Path $temporary $architecture
    Expand-Archive -LiteralPath (Join-Path $root "DesktopTodoList-$architecture-portable.zip") -DestinationPath $portableRoot
    $portableNames = @(Get-ChildItem -LiteralPath $portableRoot -File -Recurse | ForEach-Object {
      [IO.Path]::GetRelativePath($portableRoot, $_.FullName).Replace('\', '/')
    } | Sort-Object)
    $expectedPortableNames = @('DesktopTodoList.exe', 'LICENSE.txt', 'PRIVACY.md', 'README.md',
      'docs/install.md', 'docs/migrate-from-web-version.md', 'docs/troubleshooting.md') | Sort-Object
    if (Compare-Object -ReferenceObject $expectedPortableNames -DifferenceObject $portableNames) {
      throw "Portable $architecture archive has an unexpected layout."
    }
    $signatureFiles += (Join-Path $portableRoot 'DesktopTodoList.exe')
  }
  foreach ($file in $signatureFiles) {
    $path = if ([IO.Path]::IsPathRooted($file)) { $file } else { Join-Path $root $file }
    $status = (Get-AuthenticodeSignature -LiteralPath $path).Status.ToString()
    if ($SignatureStatus -eq 'valid' -and $status -ne 'Valid') {
      throw "Cannot declare a signed release: $([IO.Path]::GetFileName($path)) status is $status."
    }
    if ($SignatureStatus -eq 'unsigned' -and $status -ne 'NotSigned') {
      throw "Cannot declare an unsigned release: $([IO.Path]::GetFileName($path)) status is $status."
    }
  }
} finally {
  Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
}

$lines = @("# Signature status: $SignatureStatus")
foreach ($name in $expectedNames) {
  $hash = (Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash.ToLowerInvariant()
  $lines += "$hash  $name"
}
[IO.File]::WriteAllLines((Join-Path $root 'checksums.txt'), $lines, [Text.UTF8Encoding]::new($false))
Write-Host "Wrote six SHA-256 entries; signature status: $SignatureStatus."
