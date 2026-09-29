[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$Version,
  [Parameter(Mandatory)][string]$Artifacts,
  [string]$BuildDirectory = 'out\build\windows-x64',
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
  [string]$RequirementMatrix = 'docs\acceptance\native-requirements.md',
  [string]$RequirementsDocument = 'docs\需求文档.md',
  [string]$ReportPath
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Resolve-FromRoot([string]$Path) {
  if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
  return [IO.Path]::GetFullPath((Join-Path $root $Path))
}

$artifactPath = Resolve-FromRoot $Artifacts
$buildPath = Resolve-FromRoot $BuildDirectory
$matrixPath = Resolve-FromRoot $RequirementMatrix
$requirementsPath = Resolve-FromRoot $RequirementsDocument
$reportDirectory = Join-Path $root "out\release-verification\$Version"
if (-not $ReportPath) { $ReportPath = Join-Path $reportDirectory 'release-checklist.md' }
$reportPath = Resolve-FromRoot $ReportPath
New-Item -ItemType Directory -Path (Split-Path -Parent $reportPath) -Force | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
function Add-Result([string]$Name, [string]$Status, [string]$Evidence) {
  $results.Add([pscustomobject]@{ Name = $Name; Status = $Status; Evidence = $Evidence })
  Write-Output ("[{0}] {1}: {2}" -f $Status, $Name, $Evidence)
}
function Invoke-Check([string]$Name, [scriptblock]$Action) {
  try {
    $evidence = & $Action
    if ($null -eq $evidence -or [string]::IsNullOrWhiteSpace([string]$evidence)) { $evidence = 'verified' }
    Add-Result $Name 'PASS' ([string]$evidence)
  } catch {
    Add-Result $Name 'FAIL' ($_.Exception.Message -replace '[\r\n]+', ' ')
  }
}

$expectedArtifacts = @(
  'DesktopTodoList-x64.exe', 'DesktopTodoList-arm64.exe',
  'DesktopTodoList-x64-Setup.exe', 'DesktopTodoList-arm64-Setup.exe',
  'DesktopTodoList-x64-portable.zip', 'DesktopTodoList-arm64-portable.zip',
  'checksums.txt'
)
Invoke-Check 'Complete x64/ARM64 release artifacts' {
  if (-not (Test-Path -LiteralPath $artifactPath -PathType Container)) {
    throw "Artifact directory not found: $artifactPath"
  }
  $names = @(Get-ChildItem -LiteralPath $artifactPath -File -Force | ForEach-Object Name | Sort-Object)
  $missing = @($expectedArtifacts | Where-Object { $_ -notin $names })
  $unexpected = @($names | Where-Object { $_ -notin $expectedArtifacts })
  if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
    throw ("Missing: [{0}]; unexpected: [{1}]" -f ($missing -join ', '), ($unexpected -join ', '))
  }
  'all seven expected files are present, with no extras'
}

$ctestPath = $null
$ctestCommand = Get-Command 'ctest.exe' -ErrorAction SilentlyContinue
if ($ctestCommand) { $ctestPath = $ctestCommand.Source }
if (-not $ctestPath) {
  $cachePath = Join-Path $buildPath 'CMakeCache.txt'
  if (Test-Path -LiteralPath $cachePath) {
    $cmakeLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
    if ($cmakeLine) {
      $candidate = Join-Path (Split-Path -Parent $cmakeLine.Matches[0].Groups[1].Value) 'ctest.exe'
      if (Test-Path -LiteralPath $candidate) { $ctestPath = $candidate }
    }
  }
}
Invoke-Check 'Full x64 CTest suite' {
  if (-not $ctestPath -or -not (Test-Path -LiteralPath (Join-Path $buildPath 'CTestTestfile.cmake'))) {
    throw "No configured CTest build or ctest.exe found at $buildPath"
  }
  $output = @(& $ctestPath --test-dir $buildPath -C $Configuration --output-on-failure 2>&1)
  $code = $LASTEXITCODE
  $output | ForEach-Object { Write-Host "  $_" }
  if ($code -ne 0) { throw "CTest exited $code." }
  $summary = @($output | Where-Object { "$_" -match '100% tests passed|tests passed' } | Select-Object -Last 1)
  if ($summary.Count -gt 0) { [string]$summary[0] } else { 'CTest completed successfully' }
}

$widgetAcceptance = Join-Path $root 'native\tests\windows\widget-acceptance.ps1'
$widgetReport = Join-Path $reportDirectory 'native-widget.md'
Invoke-Check 'x64 native widget acceptance' {
  $pwshCommand = Get-Command 'pwsh.exe' -ErrorAction SilentlyContinue
  if (-not $pwshCommand) { throw 'PowerShell 7 (pwsh.exe) is required for the widget acceptance report.' }
  New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
  $output = @(& $pwshCommand.Source -NoProfile -ExecutionPolicy Bypass -File $widgetAcceptance -BuildDirectory $buildPath -Configuration $Configuration -ReportPath $widgetReport 2>&1)
  $code = $LASTEXITCODE
  $output | ForEach-Object { Write-Host "  $_" }
  if ($code -ne 0) { throw "Widget acceptance exited $code." }
  if (-not (Test-Path -LiteralPath $widgetReport -PathType Leaf)) { throw 'Widget acceptance did not create its evidence report.' }
  $reportText = [IO.File]::ReadAllText($widgetReport)
  if ($reportText -notmatch '- Result: \d+ PASS, 0 FAIL, \d+ SKIP') { throw 'Widget acceptance report contains failures or no result summary.' }
  ([regex]::Match($reportText, '- Result: [^\r\n]+').Value)
}

Invoke-Check 'Requirement IDs are mapped exactly once' {
  if (-not (Test-Path -LiteralPath $matrixPath -PathType Leaf)) { throw "Evidence matrix not found: $matrixPath" }
  if (-not (Test-Path -LiteralPath $requirementsPath -PathType Leaf)) { throw "Requirements document not found: $requirementsPath" }
  $source = [IO.File]::ReadAllText($requirementsPath)
  $matrix = [IO.File]::ReadAllText($matrixPath)
  $expectedIds = @([regex]::Matches($source, '(?m)^\|(?:[^|\r\n]*\|)*\s*((?:FR|NFR)-\d{2})\s*\|') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
  $rows = @([regex]::Matches($matrix, '(?m)^\|\s*((?:FR|NFR)-\d{2})\s*\|\s*(PASS|FAIL|SKIP)\s*\|\s*([^\r\n]+?)\s*\|$') | ForEach-Object {
    [pscustomobject]@{ Id = $_.Groups[1].Value; Status = $_.Groups[2].Value; Evidence = $_.Groups[3].Value }
  })
  $counts = @($rows | Group-Object Id | Where-Object Count -ne 1)
  if ($counts.Count -gt 0) { throw ("Requirement IDs are duplicated: " + (($counts.Name | Sort-Object -Unique) -join ', ')) }
  $actualIds = @($rows | ForEach-Object Id | Sort-Object -Unique)
  $missing = @($expectedIds | Where-Object { $_ -notin $actualIds })
  $extra = @($actualIds | Where-Object { $_ -notin $expectedIds })
  if ($missing.Count -gt 0 -or $extra.Count -gt 0) { throw ("Missing IDs: [{0}]; unknown IDs: [{1}]" -f ($missing -join ', '), ($extra -join ', ')) }
  $badSkips = @($rows | Where-Object { $_.Status -eq 'SKIP' -and ($_.Evidence -notmatch '(?i)Environment:' -or $_.Evidence -notmatch '(?i)Owner:') })
  if ($badSkips.Count -gt 0) { throw ("SKIP rows need Environment and Owner: " + (($badSkips.Id) -join ', ')) }
  $failedRows = @($rows | Where-Object Status -eq 'FAIL')
  if ($failedRows.Count -gt 0) { throw ("Requirement evidence has FAIL rows: " + (($failedRows.Id) -join ', ')) }
  $skipCount = @($rows | Where-Object Status -eq 'SKIP').Count
  "all $($expectedIds.Count) source IDs occur once; $skipCount justified SKIP rows"
}
if (Test-Path -LiteralPath $matrixPath -PathType Leaf) {
  $matrixText = [IO.File]::ReadAllText($matrixPath)
  $manualRows = @([regex]::Matches($matrixText, '(?m)^\|\s*((?:FR|NFR)-\d{2})\s*\|\s*SKIP\s*\|\s*([^\r\n]+?)\s*\|$'))
  foreach ($row in $manualRows) {
    Add-Result "Manual environment evidence: $($row.Groups[1].Value)" 'SKIP' $row.Groups[2].Value
  }
}

$docsCheck = Join-Path $root 'native\tests\release\documentation-check.ps1'
Invoke-Check 'Complete user documentation' {
  $pwshCommand = Get-Command 'pwsh.exe' -ErrorAction SilentlyContinue
  if (-not $pwshCommand) { throw 'PowerShell 7 (pwsh.exe) is required for documentation checks.' }
  $output = @(& $pwshCommand.Source -NoProfile -ExecutionPolicy Bypass -File $docsCheck 2>&1)
  $code = $LASTEXITCODE
  $output | ForEach-Object { Write-Host "  $_" }
  if ($code -ne 0) { throw "Documentation check exited $code." }
  [string]($output | Where-Object { "$_" -match '^Documentation checks:' } | Select-Object -Last 1)
}

$runtimeDirectories = @('native\app', 'native\presentation', 'native\platform\windows') | ForEach-Object { Join-Path $root $_ }
Invoke-Check 'No browser, script-host, localhost, or remote runtime dependency' {
  $forbidden = @(Get-ChildItem -LiteralPath $runtimeDirectories -Recurse -File -Include '*.cpp', '*.h' |
    Select-String -Pattern 'WebView2|CreateProcess|ShellExecute|WinExec|localhost|powershell\.exe|node\.exe')
  if ($forbidden.Count -gt 0) { throw ("Forbidden runtime reference in " + ($forbidden[0].Path)) }
  if (-not (Test-Path -LiteralPath $artifactPath -PathType Container)) { throw 'Release artifacts are unavailable for runtime dependency inspection.' }
  foreach ($name in @('DesktopTodoList-x64.exe', 'DesktopTodoList-arm64.exe')) {
    $binary = Join-Path $artifactPath $name
    if (Test-Path -LiteralPath $binary -PathType Leaf) {
      $text = [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($binary))
      $urls = @([regex]::Matches($text, '(?i)https?://[^\s"''<>]+') | ForEach-Object Value)
      $allowed = @('http://schemas.microsoft.com/SMI/2005/WindowsSettings', 'http://schemas.microsoft.com/SMI/2016/WindowsSettings')
      $unexpected = @($urls | Where-Object { $_ -notin $allowed })
      if ($unexpected.Count -gt 0) { throw "Unexpected URL in ${name}: $($unexpected[0])" }
    }
  }
  'runtime source and available executable strings have no forbidden host/network dependency'
}

$manifestScript = Join-Path $root 'scripts\release-manifest.ps1'
Invoke-Check 'Architecture/version/signature/checksum release manifest' {
  if (-not (Test-Path -LiteralPath $artifactPath -PathType Container)) { throw "Artifact directory not found: $artifactPath" }
  $pwshCommand = Get-Command 'pwsh.exe' -ErrorAction SilentlyContinue
  if (-not $pwshCommand) { throw 'PowerShell 7 (pwsh.exe) is required for the release manifest.' }
  $output = @(& $pwshCommand.Source -NoProfile -ExecutionPolicy Bypass -File $manifestScript -ArtifactsDirectory $artifactPath -Version $Version -Tag "v$Version" 2>&1)
  $code = $LASTEXITCODE
  $output | ForEach-Object { Write-Host "  $_" }
  if ($code -ne 0) { throw "Release manifest exited $code." }
  [string]($output | Select-Object -Last 1)
}

$packageCheck = Join-Path $root 'native\tests\release\package-contents.ps1'
foreach ($architecture in @('x64', 'arm64')) {
  $portableName = "DesktopTodoList-$architecture-portable.zip"
  $installerName = "DesktopTodoList-$architecture-Setup.exe"
  $packageExeSize = $null
  Invoke-Check "$architecture package contents" {
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Container)) { throw "Artifact directory not found: $artifactPath" }
    $portable = Join-Path $artifactPath $portableName
    $installer = Join-Path $artifactPath $installerName
    if (-not (Test-Path -LiteralPath $portable -PathType Leaf)) { throw "Portable archive missing: $portableName" }
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Installer missing: $installerName" }
    $pwshCommand = Get-Command 'pwsh.exe' -ErrorAction SilentlyContinue
    if (-not $pwshCommand) { throw 'PowerShell 7 (pwsh.exe) is required for package checks.' }
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $packageCheck,
      '-Package', $portable, '-Architecture', $architecture, '-Version', $Version,
      '-Installer', $installer, '-InstallerScript', (Join-Path $root 'installer\DesktopTodoList.iss'))
    if ($architecture -eq 'arm64') { $arguments += '-SkipExecutableRun' }
    $output = @(& $pwshCommand.Source @arguments 2>&1)
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host "  $_" }
    if ($code -ne 0) { throw "Package content verification exited $code." }
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-size-check-' + [guid]::NewGuid().ToString('N'))
    try {
      Expand-Archive -LiteralPath $portable -DestinationPath $scratch
      $exe = Join-Path $scratch 'DesktopTodoList.exe'
      $packageExeSize = (Get-Item -LiteralPath $exe).Length
      if ($packageExeSize -ge 5MB) { throw "$portableName executable is $packageExeSize bytes; limit is below 5 MB." }
      '{0} executable {1:N0} bytes (< 5 MB)' -f $architecture, $packageExeSize
    } finally {
      if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
    }
  }
}

if (Test-Path -LiteralPath $artifactPath -PathType Container) {
  foreach ($name in $expectedArtifacts | Where-Object { $_ -match '-Setup\.exe$|-portable\.zip$' }) {
    $path = Join-Path $artifactPath $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      Add-Result "Artifact size: $name" 'PASS' ("{0:N0} bytes" -f (Get-Item -LiteralPath $path).Length)
    }
  }
}

$failures = @($results | Where-Object Status -eq 'FAIL')
$passes = @($results | Where-Object Status -eq 'PASS')
$skips = @($results | Where-Object Status -eq 'SKIP')
$lines = @(
  '# Native release verification run'
  ''
  "- Version: $Version"
  "- Run: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss K')"
  "- Artifacts: $artifactPath"
  "- Build: $buildPath ($Configuration)"
  "- Result: $($passes.Count) PASS, $($failures.Count) FAIL, $($skips.Count) SKIP"
  ''
  '| Status | Check | Evidence |'
  '|---|---|---|'
)
$lines += @($results | ForEach-Object { '| {0} | {1} | {2} |' -f $_.Status, $_.Name, ($_.Evidence -replace '\|', '\|') })
$lines += @('', 'Manual environment-only checks are listed in `docs/acceptance/native-requirements.md`; SKIP is not PASS.')
[IO.File]::WriteAllLines($reportPath, $lines, [Text.UTF8Encoding]::new($false))
Write-Output "Release verification report: $reportPath"
Write-Output "Release gate totals: $($passes.Count) PASS, $($failures.Count) FAIL, $($skips.Count) SKIP."
if ($failures.Count -gt 0) { exit 1 }
Write-Output "Native release gate PASS for v$Version."
