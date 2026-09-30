[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$documents = @{
  'README.md' = Join-Path $repoRoot 'README.md'
  'install.md' = Join-Path $repoRoot 'docs\install.md'
  'migrate-from-web-version.md' = Join-Path $repoRoot 'docs\migrate-from-web-version.md'
  'privacy.md' = Join-Path $repoRoot 'docs\privacy.md'
  'troubleshooting.md' = Join-Path $repoRoot 'docs\troubleshooting.md'
}

$texts = @{}
foreach ($entry in $documents.GetEnumerator()) {
  if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
    throw "Required documentation file is missing: $($entry.Key)"
  }
  $texts[$entry.Key] = [IO.File]::ReadAllText($entry.Value)
}

$allText = $texts.Values -join "`n"
$requirements = [ordered]@{}
$requirements['release download choices'] = [bool]((($allText -match 'GitHub Releases') -and
  ($allText -match 'DesktopTodoList-x64-Setup\.exe') -and
  ($allText -match 'DesktopTodoList-arm64-Setup\.exe') -and
  ($allText -match 'DesktopTodoList-x64-portable\.zip') -and
  ($allText -match 'DesktopTodoList-arm64-portable\.zip')))
$requirements['x64 and ARM64 selection guidance'] = [bool](($allText -match '(?i)x64.{0,80}(ARM64|arm64)') -or
  ($allText -match '(?i)ARM64.{0,80}x64'))
$requirements['portable archive use'] = [bool](($allText -match '(?i)portable') -and ($allText -match '(?i)extract'))
$requirements['Authenticode signing and SmartScreen reputation guidance'] = [bool](
  ($texts['install.md'] -match '(?i)Authenticode') -and
  ($texts['install.md'] -match '(?i)SmartScreen') -and
  ($texts['install.md'] -match '信誉|reputation'))
$requirements['optional desktop shortcut defaults off'] = [bool](
  $texts['install.md'] -match '创建桌面快捷方式；默认不勾选')
$requirements['autostart defaults off'] = [bool](
  $texts['install.md'] -match '自动启动默认关闭')
$requirements['README feature overview'] = [bool](
  ($texts['README.md'] -match '原生 Windows 悬浮窗口') -and
  ($texts['README.md'] -match '今天、本周、全部和已完成') -and
  ($texts['README.md'] -match '快速添加') -and
  ($texts['README.md'] -match '默认关闭') -and
  ($texts['README.md'] -match '默认不创建'))
$requirements['data and backup paths'] = [bool](($allText -match '%LOCALAPPDATA%\\DesktopTodoList') -and
  ($allText -match 'data\.json') -and ($allText -match 'backups'))
$requirements['export and import migration'] = [bool](($allText -match '(?i)export JSON') -and
  ($allText -match '(?i)import JSON') -and ($allText -match 'localStorage'))
$requirements['tray recovery guidance'] = [bool](($allText -match '(?i)notification area') -and ($allText -match '(?i)tray'))
$requirements['show hotkey'] = [bool]$allText.Contains('Ctrl+Alt+T')
$requirements['interaction recovery hotkey'] = [bool]$allText.Contains('Ctrl+Alt+L')
$requirements['uninstall data behavior'] = [bool](($allText -match '(?i)uninstall') -and ($allText -match '(?i)preserve|keep') -and ($allText -match '(?i)remove user data|delete data'))
$requirements['offline and no telemetry'] = [bool](($texts['privacy.md'] -match '(?i)offline') -and ($texts['privacy.md'] -match '(?i)telemetry') -and ($texts['privacy.md'] -match '(?i)not upload|never upload'))
$requirements['notification troubleshooting'] = [bool](($texts['troubleshooting.md'] -match '(?i)notification') -and ($texts['troubleshooting.md'] -match '(?i)tray'))
$requirements['hotkey troubleshooting'] = [bool](($texts['troubleshooting.md'] -match '(?i)hotkey') -and ($texts['troubleshooting.md'] -match '(?i)conflict|occupied'))
$requirements['autostart troubleshooting'] = [bool](($texts['troubleshooting.md'] -match '(?i)autostart') -and ($texts['troubleshooting.md'] -match '(?i)settings'))
$requirements['no automatic Edge localStorage extraction'] = [bool](($texts['migrate-from-web-version.md'] -match 'localStorage') -and ($texts['migrate-from-web-version.md'] -match '(?i)not automatic|not supported') -and ($texts['migrate-from-web-version.md'] -match '(?i)manual export'))

$failed = @($requirements.GetEnumerator() | Where-Object { -not $_.Value })
if ($failed.Count -gt 0) {
  $failed | ForEach-Object { Write-Output "FAIL: $($_.Key)" }
  throw "$($failed.Count) documentation requirement(s) are missing."
}

$requirements.Keys | ForEach-Object { Write-Output "PASS: $_" }
Write-Output "Documentation checks: $($requirements.Count)/$($requirements.Count) passed."
