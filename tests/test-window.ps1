# =============================================================================
# tests/test-window.ps1 — 用真实窗口验证 Win32 控制逻辑
# -----------------------------------------------------------------------------
# 需求对应：FR-72 置顶 / FR-73 置底 / FR-74 穿透 / FR-75 几何
#
# 做法：起一个记事本窗口当靶子，对它施加各种窗口操作，然后回读
#       GetWindowLong(GWL_EXSTYLE) 与 GetWindowRect 验证真实生效，
#       而不是只看函数返回值。
# 运行：powershell -ExecutionPolicy Bypass -File tests\test-window.ps1
# =============================================================================
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DwxRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

$passed = 0
$failed = @()

function Check {
  param([string]$Label, [bool]$Cond, [string]$Detail = '')
  if ($Cond) {
    $script:passed++
    Write-Host "  [OK]   $Label" -ForegroundColor Green
  } else {
    $script:failed += ($Label + $(if ($Detail) { "  [$Detail]" } else { '' }))
    Write-Host "  [FAIL] $Label  $Detail" -ForegroundColor Red
  }
}

function Get-Field {
  <#
    StrictMode 下访问哈希中不存在的键会直接抛异常，
    而我们的函数返回的是"成功时不带 error 键、失败时不带 layer 键"的哈希。
    断言里读值必须走这个助手。
  #>
  param($Obj, [string]$Name)
  if ($null -eq $Obj) { return $null }
  if ($Obj -is [hashtable]) {
    if ($Obj.ContainsKey($Name)) { return $Obj[$Name] }
    return $null
  }
  if ($Obj.PSObject.Properties.Name -contains $Name) { return $Obj.$Name }
  return $null
}

Write-Host ''
Write-Host '加载窗口模块…' -ForegroundColor Cyan
. (Join-Path $DwxRoot 'host\window.ps1')

# ------------------------------------------------------------ 纯逻辑部分
Write-Host ''
Write-Host '热键解析' -ForegroundColor Cyan

$spec = ConvertTo-HotkeySpec -Text 'Ctrl+Alt+L'
Check 'Ctrl+Alt+L 可解析' ($null -ne $spec)
Check '修饰键含 Ctrl 与 Alt' (($spec.modifiers -band 0x0002) -ne 0 -and ($spec.modifiers -band 0x0001) -ne 0) ("mods=" + $spec.modifiers)
Check '虚拟键为 L (0x4C)' ($spec.vk -eq 0x4C) ("vk=0x{0:X}" -f $spec.vk)

$spec2 = ConvertTo-HotkeySpec -Text 'Ctrl+Shift+F5'
Check 'Ctrl+Shift+F5 可解析' ($spec2.vk -eq 0x74) ("vk=0x{0:X}" -f $spec2.vk)

Check '无修饰键被拒绝' ($null -eq (ConvertTo-HotkeySpec -Text 'L'))
Check '拼错的修饰键被拒绝' ($null -eq (ConvertTo-HotkeySpec -Text 'Ctrk+L'))
Check '空字符串被拒绝' ($null -eq (ConvertTo-HotkeySpec -Text ''))

# ------------------------------------------------------------ 屏幕与几何
Write-Host ''
Write-Host '屏幕与几何判断' -ForegroundColor Cyan

$wa = Get-PrimaryWorkArea
Check '能取到工作区尺寸' ($wa.w -gt 0 -and $wa.h -gt 0) ("$($wa.w)x$($wa.h) @ $($wa.x),$($wa.y)")

$inside = [pscustomobject]@{ x = $wa.x + 100; y = $wa.y + 100; w = 400; h = 400 }
Check '屏幕内窗口判定为可见' (Test-BoundsOnScreen -Bounds $inside -WorkArea $wa)

$offscreen = [pscustomobject]@{ x = $wa.x + $wa.w + 5000; y = $wa.y + 100; w = 400; h = 400 }
Check '屏幕外窗口判定为不可见（FR-75 防跑丢）' (-not (Test-BoundsOnScreen -Bounds $offscreen -WorkArea $wa))

$wayLeft = [pscustomobject]@{ x = $wa.x - 5000; y = $wa.y + 100; w = 400; h = 400 }
Check '负坐标远处窗口判定为不可见' (-not (Test-BoundsOnScreen -Bounds $wayLeft -WorkArea $wa))

# ------------------------------------------------------------ 开机自启（无副作用）
Write-Host ''
Write-Host '开机自启注册表' -ForegroundColor Cyan

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$hadValue = $false
$originalValue = $null
try {
  $existing = Get-ItemProperty -Path $runKey -Name 'DesktopTodoList' -ErrorAction Stop
  $hadValue = $true
  $originalValue = $existing.DesktopTodoList
} catch { $hadValue = $false }

# 用一个明确的假路径，验证关闭逻辑与错误报告
$state = Get-AutoStartState -LauncherPath 'C:\nonexistent\launcher.ps1'
Check 'Get-AutoStartState 可读（不抛异常）' ($null -ne $state)

$missing = Set-AutoStart -Enabled $true -LauncherPath 'C:\definitely\not\here\launcher.ps1'
Check '开启自启时校验脚本存在性' ($missing.ok -eq $false)
Check '缺失脚本时给出可读错误' ($missing.error -match '找不到启动脚本') ("error=$(Get-Field $missing 'error')")

# 真正写入一次并回读（用完立即还原，避免污染用户环境）
if (-not $hadValue) {
  $written = Set-AutoStart -Enabled $true -LauncherPath (Join-Path $DwxRoot 'host\launcher.ps1')
  if (-not $written.ok -and (Get-Field $written 'error') -match 'denied|拒绝|权限|access') {
    Write-Host '  [SKIP] 当前环境禁止写入 HKCU Run，跳过真实注册表写入测试' -ForegroundColor DarkYellow
  } else {
    Check '开启自启写入成功' ($written.ok -eq $true) ("error=$(Get-Field $written 'error')")

    $after = Get-AutoStartState -LauncherPath (Join-Path $DwxRoot 'host\launcher.ps1')
    Check '回读显示已开启' ($after.enabled -eq $true)
    Check '注册表命令包含 -AutoStart' ($after.command -match '-AutoStart') ("cmd=$(Get-Field $after 'command')")
    Check '注册表命令包含 launcher.ps1' ($after.command -match 'launcher\.ps1')

    $removed = Set-AutoStart -Enabled $false -LauncherPath (Join-Path $DwxRoot 'host\launcher.ps1')
    Check '关闭自启成功' ($removed.ok -eq $true)
    $after2 = Get-AutoStartState -LauncherPath (Join-Path $DwxRoot 'host\launcher.ps1')
    Check '关闭后回读为未开启' ($after2.enabled -eq $false)
  }
} else {
  Write-Host '  [SKIP] 检测到已存在的自启项，跳过写入测试以免影响现有配置' -ForegroundColor DarkYellow
}

# ------------------------------------------------------------ 真实窗口操作
Write-Host ''
Write-Host '真实窗口操作（以记事本为靶子）' -ForegroundColor Cyan

$target = $null
try {
  $target = Start-Process notepad.exe -PassThru
} catch {
  Write-Host "  无法启动记事本：$($_.Exception.Message)" -ForegroundColor Red
}

$hwnd = $null
if ($target) {
  # 注意：Start-Process 返回的 Process 对象其 MainWindowHandle 常为 0——
  # Win11 的 Notepad 是 Store 应用，窗口由另一个宿主进程持有。
  # 因此走与生产代码相同的路径：按窗口标题枚举查找。
  $hwnd = Get-ProcessMainWindowHandle -TitleLike '*Notepad*' -TimeoutSeconds 10
  if ($null -eq $hwnd -or [Int64]$hwnd -eq 0) {
    $hwnd = Get-ProcessMainWindowHandle -TitleLike '*记事本*' -TimeoutSeconds 5
  }
}

if ($null -eq $hwnd -or [Int64]$hwnd -eq 0) {
  Write-Host '  [SKIP] 未能取得靶窗口句柄，跳过真实窗口测试' -ForegroundColor DarkYellow
} else {
  Write-Host ("  靶窗口 HWND = 0x{0:X}" -f ([Int64]$hwnd)) -ForegroundColor DarkGray

  # --- 基础几何 ---
  $b0 = Get-WindowBounds -Hwnd $hwnd
  Check '能读取窗口矩形' ($null -ne $b0 -and $b0.w -gt 0 -and $b0.h -gt 0) ("$($b0.w)x$($b0.h)")

  # --- 穿透：关闭可选中 ---
  $r = Set-WindowSelectable -Hwnd $hwnd -Selectable $false
  Check '设置不可选中返回成功' ($r.ok -eq $true) ("error=$(Get-Field $r 'error')")
  $ex = [DesktopTodo.Win32]::GetWindowLongPtr($hwnd, [DesktopTodo.Win32]::GWL_EXSTYLE).ToInt64()
  Check 'WS_EX_TRANSPARENT 已置位' (($ex -band [int64][DesktopTodo.Win32]::WS_EX_TRANSPARENT) -ne 0) ("exstyle=0x{0:X}" -f $ex)
  Check 'WS_EX_LAYERED 已置位' (($ex -band [int64][DesktopTodo.Win32]::WS_EX_LAYERED) -ne 0)
  Check 'WS_EX_NOACTIVATE 已置位' (($ex -band [int64][DesktopTodo.Win32]::WS_EX_NOACTIVATE) -ne 0)
  Check '穿透状态可回读为 false' ((Get-WindowSelectable -Hwnd $hwnd) -eq $false)

  # --- 恢复可选中 ---
  $r = Set-WindowSelectable -Hwnd $hwnd -Selectable $true
  Check '恢复可选中返回成功' ($r.ok -eq $true)
  $ex2 = [DesktopTodo.Win32]::GetWindowLongPtr($hwnd, [DesktopTodo.Win32]::GWL_EXSTYLE).ToInt64()
  Check 'WS_EX_TRANSPARENT 已清除' (($ex2 -band [int64][DesktopTodo.Win32]::WS_EX_TRANSPARENT) -eq 0) ("exstyle=0x{0:X}" -f $ex2)
  Check 'WS_EX_NOACTIVATE 已清除' (($ex2 -band [int64][DesktopTodo.Win32]::WS_EX_NOACTIVATE) -eq 0)
  Check '可选中状态可回读为 true' ((Get-WindowSelectable -Hwnd $hwnd) -eq $true)

  # --- 层级：置顶 / 普通 / 置底 ---
  foreach ($layer in @('top', 'normal', 'bottom')) {
    $r = Set-WindowLayer -Hwnd $hwnd -Layer $layer
    Check "设置层级 '$layer' 返回成功" ($r.ok -eq $true) ("error=$(Get-Field $r 'error')")
  }

  # 置底后再调用普通层级，应恢复为可交互状态
  Check '层级切换后窗口仍有效' ([DesktopTodo.Win32]::IsWindow($hwnd))

  # --- 移动 ---
  $r = Set-WindowLayer -Hwnd $hwnd -Layer 'normal'
  $before = Get-WindowBounds -Hwnd $hwnd
  $r = Move-WindowBy -Hwnd $hwnd -Dx 37 -Dy 23 -Dpr 1
  Check '按位移移动返回成功' ($r.ok -eq $true) ("error=$(Get-Field $r 'error')")
  $after = Get-WindowBounds -Hwnd $hwnd
  Check 'X 位移生效' ([Math]::Abs(($after.x - $before.x) - 37) -le 1) ("dx=" + ($after.x - $before.x))
  Check 'Y 位移生效' ([Math]::Abs(($after.y - $before.y) - 23) -le 1) ("dy=" + ($after.y - $before.y))

  # --- 缩放 ---
  $r = Resize-WindowBy -Hwnd $hwnd -Dw 50 -Dh 40 -Dpr 1
  Check '按增量缩放返回成功' ($r.ok -eq $true) ("error=$(Get-Field $r 'error')")
  $after2 = Get-WindowBounds -Hwnd $hwnd
  Check '宽度增加约 50' ([Math]::Abs(($after2.w - $after.w) - 50) -le 2) ("dw=" + ($after2.w - $after.w))
  Check '高度增加约 40' ([Math]::Abs(($after2.h - $after.h) - 40) -le 2) ("dh=" + ($after2.h - $after.h))

  # --- 最小尺寸保护（FR-71 浮窗不应被缩到不可用）---
  $r = Resize-WindowBy -Hwnd $hwnd -Dw -5000 -Dh -5000 -Dpr 1 -MinW 260 -MinH 240
  $tiny = Get-WindowBounds -Hwnd $hwnd
  Check '缩放过小时受最小尺寸约束' ($tiny.w -ge 260 -and $tiny.h -ge 240) ("$($tiny.w)x$($tiny.h)")

  # --- 绝对几何 ---
  $r = Set-WindowBounds -Hwnd $hwnd -X ($wa.x + 120) -Y ($wa.y + 90) -W 420 -H 360 -Dpr 1
  $abs = Get-WindowBounds -Hwnd $hwnd
  Check '绝对位置设置生效' ([Math]::Abs($abs.x - ($wa.x + 120)) -le 2 -and [Math]::Abs($abs.y - ($wa.y + 90)) -le 2) ("$($abs.x),$($abs.y)")
  Check '绝对尺寸设置生效' ([Math]::Abs($abs.w - 420) -le 2 -and [Math]::Abs($abs.h - 360) -le 2) ("$($abs.w)x$($abs.h)")

  # --- 显隐 ---
  Hide-Window -Hwnd $hwnd
  Start-Sleep -Milliseconds 150
  Check '隐藏窗口生效' (-not [DesktopTodo.Win32]::IsWindowVisible($hwnd))

  Show-WindowSafe -Hwnd $hwnd -NoActivate
  Start-Sleep -Milliseconds 250
  Check '显示窗口生效' ([DesktopTodo.Win32]::IsWindowVisible($hwnd))

  Minimize-Window -Hwnd $hwnd
  Start-Sleep -Milliseconds 300
  Check '最小化生效' ([DesktopTodo.Win32]::IsWindowIconic($hwnd))

  # --- 无效句柄的健壮性 ---
  $bogus = [IntPtr]0x1234
  $r = Set-WindowLayer -Hwnd $bogus -Layer 'top'
  Check '无效句柄：置顶返回失败而非崩溃' ($r.ok -eq $false)
  $r = Set-WindowSelectable -Hwnd $bogus -Selectable $false
  Check '无效句柄：穿透返回失败而非崩溃' ($r.ok -eq $false)
  Check '无效句柄：读取矩形返回 null' ($null -eq (Get-WindowBounds -Hwnd $bogus))
  Check '无效句柄：移动返回失败' ((Move-WindowBy -Hwnd $bogus -Dx 1 -Dy 1).ok -eq $false)

  # --- 优雅关闭 ---
  Close-WindowGracefully -Hwnd $hwnd
  Start-Sleep -Milliseconds 800
  Check '发送 WM_CLOSE 后窗口被销毁' (-not [DesktopTodo.Win32]::IsWindow($hwnd))
}

# ------------------------------------------------------------ 收尾
if ($target) {
  try { if (-not $target.HasExited) { $target.Kill() } } catch { }
}

Write-Host ''
Write-Host ('-' * 58)
if ($failed.Count -eq 0) {
  Write-Host "全部通过  $passed 项断言" -ForegroundColor Green
  exit 0
} else {
  Write-Host "失败 $($failed.Count) 项（通过 $passed 项）" -ForegroundColor Red
  $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
  exit 1
}
