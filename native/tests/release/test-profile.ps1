[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Root,
  [Parameter(Mandatory)][ValidateSet('Seed', 'AssertPreserved', 'AssertRemoved')][string]$Mode
)

$ErrorActionPreference = 'Stop'
$safeParent = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) 'DesktopTodoListTestProfiles'))
$rootPath = [IO.Path]::GetFullPath($Root)
$relative = [IO.Path]::GetRelativePath($safeParent, $rootPath)
$profileGuid = [guid]::Empty
if ($relative.StartsWith('..') -or [IO.Path]::IsPathRooted($relative) -or
    $relative.Contains([IO.Path]::DirectorySeparatorChar) -or
    -not [guid]::TryParse($relative, [ref]$profileGuid)) {
  throw "Test profile must be a GUID directory directly under $safeParent"
}

$dataDirectory = Join-Path $rootPath 'LocalAppData\DesktopTodoList'
$dataFile = Join-Path $dataDirectory 'data.json'
$backupFile = Join-Path $dataDirectory 'backups\seeded.json'
$outsideMarker = Join-Path $rootPath 'must-survive-data-removal.txt'
$registryName = "DesktopTodoListTest_$($profileGuid.ToString('N'))"
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$expectedFile = Join-Path $rootPath 'expected-sha256.json'

switch ($Mode) {
  'Seed' {
    if (Test-Path -LiteralPath $rootPath) { throw "Refusing to reuse profile: $rootPath" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $dataFile) -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
    $fixturePath = Join-Path $PSScriptRoot '..\fixtures\schema-v1-export.json'
    $json = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $fixturePath).Path)
    if ($json -notmatch '"autoStart":false') { throw 'Fixture auto-start setting changed; update this test explicitly.' }
    $json = $json.Replace('"autoStart":false', '"autoStart":true')
    [IO.File]::WriteAllText($dataFile, $json, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($backupFile, $json, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($outsideMarker, 'keep-outside-data-root', [Text.UTF8Encoding]::new($false))
    $hashes = [ordered]@{
      Data = (Get-FileHash -LiteralPath $dataFile -Algorithm SHA256).Hash
      Backup = (Get-FileHash -LiteralPath $backupFile -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText($expectedFile, ($hashes | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    New-Item -Path $runKey -Force | Out-Null
    New-ItemProperty -Path $runKey -Name $registryName -Value ('"' + (Join-Path $rootPath 'OldProgram\DesktopTodoList.exe') + '"') -PropertyType String -Force | Out-Null
    [pscustomobject]@{ Root = $rootPath; DataDirectory = $dataDirectory; RegistryName = $registryName; OutsideMarker = $outsideMarker }
  }
  'AssertPreserved' {
    if (-not (Test-Path -LiteralPath $dataFile) -or -not (Test-Path -LiteralPath $backupFile)) {
      throw 'Default uninstall or upgrade did not preserve seeded data and backup.'
    }
    $expected = Get-Content -LiteralPath $expectedFile -Raw | ConvertFrom-Json
    if ((Get-FileHash -LiteralPath $dataFile -Algorithm SHA256).Hash -ne $expected.Data) { throw 'Seeded data.json changed.' }
    if ((Get-FileHash -LiteralPath $backupFile -Algorithm SHA256).Hash -ne $expected.Backup) { throw 'Seeded backup changed.' }
    Write-Host '[PASS] Seeded data and backup remain byte-for-byte unchanged'
  }
  'AssertRemoved' {
    if (Test-Path -LiteralPath $dataDirectory) { throw 'Explicit data removal did not remove the exact application data directory.' }
    if (-not (Test-Path -LiteralPath $outsideMarker)) { throw 'Explicit data removal escaped the application data directory.' }
    Write-Host '[PASS] Explicit data removal deleted only the application data directory'
  }
}
