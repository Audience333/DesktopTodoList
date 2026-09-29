[CmdletBinding()]
param(
  [string]$BuildDirectory = 'out\build\windows-x64',
  [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'Debug',
  [string]$ReportPath
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
  [IO.Path]::GetFullPath($BuildDirectory)
} else {
  [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
}
if (-not $ReportPath) { $ReportPath = Join-Path $root 'docs\acceptance\native-widget.md' }
$reportPath = [IO.Path]::GetFullPath($ReportPath)
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result([string]$Capability, [string]$Status, [string]$Evidence) {
  $results.Add([pscustomobject]@{ Capability = $Capability; Status = $Status; Evidence = $Evidence })
  Write-Host ("[{0}] {1} — {2}" -f $Status, $Capability, $Evidence)
}

function Find-BuildOutput([string[]]$Candidates) {
  foreach ($candidate in $Candidates) {
    $path = Join-Path $build $candidate
    if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
  }
  return $null
}

$app = Find-BuildOutput @("native\$Configuration\DesktopTodoList.exe", 'native\DesktopTodoList.exe')
$testExe = Find-BuildOutput @("native\tests\$Configuration\desktop_todo_tests.exe", 'native\tests\desktop_todo_tests.exe')
$ctest = Get-Command 'ctest.exe' -ErrorAction SilentlyContinue
if (-not $ctest) {
  $cache = Join-Path $build 'CMakeCache.txt'
  if (Test-Path -LiteralPath $cache) {
    $cmakeLine = Select-String -LiteralPath $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
    if ($cmakeLine) {
      $candidate = Join-Path (Split-Path -Parent $cmakeLine.Matches[0].Groups[1].Value) 'ctest.exe'
      if (Test-Path -LiteralPath $candidate) { $ctest = Get-Item -LiteralPath $candidate }
    }
  }
}

$ctestPath = if ($ctest.Source) { $ctest.Source } elseif ($ctest.FullName) { $ctest.FullName } else { $null }
$ctestOutput = @()
$ctestExit = 127
if ($ctestPath -and (Test-Path -LiteralPath (Join-Path $build 'CTestTestfile.cmake'))) {
  $ctestOutput = @(& $ctestPath --test-dir $build -C $Configuration --output-on-failure 2>&1)
  $ctestExit = $LASTEXITCODE
  $ctestOutput | ForEach-Object { Write-Host $_ }
  if ($ctestExit -eq 0) {
    Add-Result '完整原生自动化测试' 'PASS' 'CTest 全量套件通过。'
  } else {
    Add-Result '完整原生自动化测试' 'FAIL' "CTest 退出码 $ctestExit。"
  }
} else {
  Add-Result '完整原生自动化测试' 'FAIL' '未找到配置好的 CTest 构建目录或 ctest.exe。'
}

$categoryTests = @(
  @{ Name = '单实例及二次启动协议'; Tests = @('native.single_instance') },
  @{ Name = '任务增删改、撤销与四种视图'; Tests = @('native.task_store', 'native.task_query', 'native.app_service') },
  @{ Name = '编辑、搜索和原生输入'; Tests = @('native.editing', 'native.editor_host_smoke') },
  @{ Name = '多选、框选和排序交互'; Tests = @('native.pointer_controller', 'native.selection_toolbar') },
  @{ Name = '导入、导出和重置'; Tests = @('native.import_export', 'native.data_transfer_dialog') },
  @{ Name = '设置与事务回滚'; Tests = @('native.settings_panel') },
  @{ Name = '通知栏和托盘菜单模型'; Tests = @('native.tray_menu') },
  @{ Name = '全局快捷键'; Tests = @('native.hotkey_service') },
  @{ Name = '窗口层级与点击穿透恢复'; Tests = @('native.window_behavior') },
  @{ Name = '提醒通知及失败重试'; Tests = @('native.notification_service') },
  @{ Name = '当前用户自启动注册逻辑'; Tests = @('native.autostart_service') },
  @{ Name = '无障碍语义和窗口 UIA 接口'; Tests = @('native.accessibility', 'native.widget_window_smoke') },
  @{ Name = '布局、DPI 缩放和屏幕边界模型'; Tests = @('native.layout', 'native.renderer_state') },
  @{ Name = '持久化、退出刷新逻辑'; Tests = @('native.state_repository', 'native.app_service') },
  @{ Name = '1000 项任务虚拟化与渲染性能'; Tests = @('native.task_list_view', 'native.renderer_smoke') }
)

if ($ctestExit -eq 0) {
  foreach ($group in $categoryTests) {
    Add-Result $group.Name 'PASS' ("覆盖测试：" + ($group.Tests -join ', '))
  }
} elseif ($ctestPath) {
  $failedNames = @($ctestOutput | ForEach-Object {
    if ("$_" -match '^\s*\d+/\d+ Test\s+#\d+:\s+(\S+)\s+\.+\s+\*\*\*Failed') { $Matches[1] }
  })
  foreach ($group in $categoryTests) {
    $failed = @($group.Tests | Where-Object { $failedNames -contains $_ })
    if ($failed.Count -gt 0) {
      Add-Result $group.Name 'FAIL' ("失败测试：" + ($failed -join ', '))
    } else {
      Add-Result $group.Name 'SKIP' 'CTest 未完整通过，暂不能给此组出具通过结论。'
    }
  }
}

if ($app -and $testExe) {
  $fixture = Join-Path $root 'native\tests\fixtures\schema-v1-export.json'
  $before = @(Get-Process -ErrorAction SilentlyContinue | ForEach-Object { $_.Id })
  $watch = [Diagnostics.Stopwatch]::StartNew()
  $verify = Start-Process -FilePath $app -ArgumentList @('--verify-core', ('"{0}"' -f $fixture)) -Wait -PassThru -WindowStyle Hidden
  $watch.Stop()
  if ($verify.ExitCode -eq 0) {
    Add-Result '本机状态载入、导入和强制落盘' 'PASS' ("--verify-core 返回 0，用时 {0:N0} ms。" -f $watch.Elapsed.TotalMilliseconds)
  } else {
    Add-Result '本机状态载入、导入和强制落盘' 'FAIL' ("--verify-core 返回 $($verify.ExitCode)。")
  }
  $after = @(Get-Process -ErrorAction SilentlyContinue)
  $newProcesses = @($after | Where-Object { $before -notcontains $_.Id })
  $browserNames = @('chrome', 'msedge', 'msedgewebview2', 'firefox', 'iexplore', 'node', 'powershell', 'pwsh')
  $unexpected = @($newProcesses | Where-Object { $browserNames -contains $_.ProcessName.ToLowerInvariant() })
  $runtimeDirectories = @('native\app', 'native\presentation', 'native\platform\windows') |
    ForEach-Object { Join-Path $root $_ }
  $forbiddenCalls = @(Get-ChildItem -Path $runtimeDirectories -Recurse -File -Include '*.cpp', '*.h' |
    Select-String -Pattern 'WebView2|CreateProcess|ShellExecute|WinExec|localhost|powershell\.exe|node\.exe')
  if ($unexpected.Count -eq 0 -and $forbiddenCalls.Count -eq 0) {
    Add-Result '运行过程未创建浏览器、WebView、localhost 或 PowerShell 宿主' 'PASS' '核心验收前后未出现这些新进程，且原生运行路径未调用 WebView、浏览器/脚本子进程或 localhost 服务入口。'
  } else {
    $evidence = @()
    if ($unexpected.Count -gt 0) { $evidence += "新进程：" + (($unexpected.ProcessName | Sort-Object -Unique) -join ', ') }
    if ($forbiddenCalls.Count -gt 0) { $evidence += "原生运行路径出现禁用调用：" + (($forbiddenCalls.Path | Sort-Object -Unique) -join ', ') }
    Add-Result '运行过程未创建浏览器、WebView、localhost 或 PowerShell 宿主' 'FAIL' ($evidence -join '；')
  }
} else {
  Add-Result '本机状态载入、导入和强制落盘' 'FAIL' '未找到原生程序或原生测试程序。'
  Add-Result '运行过程未创建浏览器、WebView、localhost 或 PowerShell 宿主' 'FAIL' '未找到可供验收的原生构建产物。'
}

$activeApps = @(Get-Process -Name 'DesktopTodoList' -ErrorAction SilentlyContinue)
$activePath = if ($activeApps.Count -gt 0) { $activeApps[0].Path } else { $null }
$interactiveSkip = if ($activeApps.Count -gt 0) {
  "已有程序运行中（PID $($activeApps[0].Id)）；为避免触碰其单实例窗口，不启动或关闭 GUI 验收实例。"
} else {
  '本次自动验收不启动交互式 GUI；需在专用桌面会话手动执行相应检查。'
}
Add-Result '冷启动耗时（≤1 秒）及空闲内存（<120 MB）' 'SKIP' $interactiveSkip
Add-Result '真实双开时只保留一个窗口并激活既有实例' 'SKIP' $interactiveSkip
Add-Result '真实托盘图标、快捷键、Explorer 重启及点击穿透恢复' 'SKIP' $interactiveSkip
Add-Result 'Windows 通知中心 Toast 实际呈现与托盘气泡回退' 'SKIP' $interactiveSkip
Add-Result 'HKCU\Run 实际写入/删除' 'SKIP' '自动测试使用注入接口，不改动当前用户的启动项；需要专用账户做交互验证。'
Add-Result 'Narrator / Inspect 屏幕阅读器实测' 'SKIP' $interactiveSkip
Add-Result '真实 DPI、多显示器拔插与窗口恢复' 'SKIP' '布局和缩放模型有自动测试；本次未改变显示设置或拔插显示器。'
Add-Result '真实文件对话框、拖放、关窗留托盘和退出落盘' 'SKIP' $interactiveSkip

$osBuild = '未知'
try {
  $os = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
  $osBuild = "$($os.ProductName) build $($os.CurrentBuild).$($os.UBR)"
} catch { }
$systemDpi = '未读取'
try {
  Add-Type -TypeDefinition 'using System.Runtime.InteropServices; namespace Acceptance { public static class NativeDpi { [DllImport("user32.dll")] public static extern uint GetDpiForSystem(); } }' -ErrorAction SilentlyContinue
  $systemDpi = [Acceptance.NativeDpi]::GetDpiForSystem()
} catch { }
$monitorCount = '未读取'
try {
  Add-Type -AssemblyName System.Windows.Forms
  $monitorCount = [System.Windows.Forms.Screen]::AllScreens.Count
} catch { }

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'
$passCount = @($results | Where-Object Status -eq 'PASS').Count
$failCount = @($results | Where-Object Status -eq 'FAIL').Count
$skipCount = @($results | Where-Object Status -eq 'SKIP').Count
$table = @($results | ForEach-Object { '| {0} | {1} | {2} |' -f $_.Status, $_.Capability, ($_.Evidence -replace '\|', '\|') })
$markdown = @(
  '# Native desktop widget acceptance report'
  ''
  "- Run: $stamp"
  "- OS: $osBuild"
  "- Architecture: $([Runtime.InteropServices.RuntimeInformation]::OSArchitecture)"
  "- System DPI: $systemDpi"
  "- Monitors: $monitorCount"
  "- Configuration: $Configuration"
  "- Build directory: $build"
  "- Existing DesktopTodoList process: $(if ($activePath) { "$activePath (PID $($activeApps[0].Id))" } else { 'none' })"
  "- Result: $passCount PASS, $failCount FAIL, $skipCount SKIP"
  ''
  '| Status | Capability | Evidence / reason |'
  '|---|---|---|'
  $table
  ''
  'SKIP items are environment-limited or intentionally avoid modifying the active user session; they are not counted as passes.'
)
$reportDirectory = Split-Path -Parent $reportPath
if (-not (Test-Path -LiteralPath $reportDirectory)) { New-Item -Path $reportDirectory -ItemType Directory -Force | Out-Null }
[IO.File]::WriteAllText($reportPath, ($markdown -join [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
Write-Host "Acceptance report: $reportPath"
Write-Host ("Summary: {0} PASS, {1} FAIL, {2} SKIP" -f $passCount, $failCount, $skipCount)
if ($failCount -gt 0) { exit 1 }
exit 0
