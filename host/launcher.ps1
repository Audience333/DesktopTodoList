# =============================================================================
# launcher.ps1 — 宿主主程序
# -----------------------------------------------------------------------------
# 职责：
#   1. 单实例保护、启动本地静态服务器作为 keepalive
#   2. 用 Edge --app 模式打开页面，取得窗口句柄（HWND）
#   3. 应用窗口层级 / 鼠标穿透 / 初始几何
#   4. 注册全局热键（显隐窗口、切换穿透）——穿透的逃生通道之一（FR-77）
#   5. 托盘图标与右键菜单，并保持消息循环直到退出
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File host\launcher.ps1
#   powershell ... -File host\launcher.ps1 -AutoStart      # 由开机自启调用
#   powershell ... -File host\launcher.ps1 -Stop           # 结束已在运行的实例
#   powershell ... -File host\launcher.ps1 -Toggle         # 显示/隐藏已运行实例
# =============================================================================

[CmdletBinding()]
param(
  [switch]$AutoStart,     # 由注册表 Run 项传入：按设置决定是否直接隐藏窗口
  [switch]$Stop,          # 停止已在运行的实例
  [switch]$Toggle,        # 切换已运行实例的显示状态
  [int]$Port = 0          # 指定端口（默认自动挑选）
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# $PSCommandPath 在 -File 调用下始终可靠；$MyInvocation.MyCommand.Path 在某些
# 宿主环境下会为空，曾导致 SiteRoot 变成空字符串而直接启动失败。
$HostDir  = Split-Path -Parent $PSCommandPath
$RootDir  = Split-Path -Parent $HostDir
$SiteRoot = Join-Path $RootDir 'src'

$StateDir    = Join-Path $env:LOCALAPPDATA 'DesktopTodoList'
$SettingsFile = Join-Path $StateDir 'host-settings.json'
$LogFile      = Join-Path $StateDir 'host.log'
$ProfileDir   = Join-Path $StateDir 'edge-profile'

$script:MutexName   = 'Global\DesktopTodoList.Host.SingleInstance'
$script:Mutex       = $null
$script:Hwnd        = [IntPtr]::Zero
$script:MsgHwnd     = [IntPtr]::Zero
$script:EdgeProc    = $null
$script:Settings    = $null
$script:Running     = $true
$script:HotkeyShow  = 1
$script:HotkeySel   = 2
$script:HotkeyShowOk = $false
$script:HotkeySelOk  = $false
$script:GeometryDirty = $false
$script:GeometryTimer = $null
$script:TrayReady   = $false
$script:PendingCount = 0

# ------------------------------------------------------------------ 日志
function Write-Log {
  param([string]$Message, [string]$Level = 'INFO')
  $line = '[{0}] [{1}] {2}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Level, $Message
  Write-Host $line
  try {
    if (-not (Test-Path -LiteralPath $StateDir)) { New-Item -ItemType Directory -Path $StateDir -Force | Out-Null }
    Add-Content -LiteralPath $LogFile -Value $line -Encoding UTF8
  } catch { }
}

# ------------------------------------------------------------------ 设置读写
function New-DefaultSettings {
  return [ordered]@{
    mode             = 'normal'
    layer            = 'normal'
    selectable       = $true
    autoStart        = $false
    startMinimized   = $false
    geometry         = @{ x = $null; y = $null; w = 380; h = 520 }
    dpr              = 1
    hotkey           = 'Ctrl+Alt+T'
    hotkeySelectable = 'Ctrl+Alt+L'
  }
}

function Read-HostSettings {
  if (-not (Test-Path -LiteralPath $SettingsFile)) { return New-DefaultSettings }
  try {
    $raw = Get-Content -LiteralPath $SettingsFile -Raw -Encoding UTF8
    if (-not $raw) { return New-DefaultSettings }
    $obj = $raw | ConvertFrom-Json
    if (-not $obj) { return New-DefaultSettings }

    $d = New-DefaultSettings
    $out = [ordered]@{}
    foreach ($k in $d.Keys) {
      $v = $obj.$k
      if ($null -eq $v) { $out[$k] = $d[$k] } else { $out[$k] = $v }
    }
    if ($out.geometry -isnot [System.Management.Automation.PSCustomObject] -and $out.geometry -isnot [hashtable]) {
      $out.geometry = $d.geometry
    } else {
      $g = [ordered]@{}
      foreach ($gk in @('x', 'y', 'w', 'h')) {
        $gv = $out.geometry.$gk
        if ($null -eq $gv -and $gk -in @('x', 'y')) { $g[$gk] = $null }
        elseif ($null -eq $gv) { $g[$gk] = $d.geometry[$gk] }
        else { $g[$gk] = [int]$gv }
      }
      $out.geometry = $g
    }
    return $out
  } catch {
    Write-Log "读取设置失败，使用默认值：$($_.Exception.Message)" 'WARN'
    return New-DefaultSettings
  }
}

function Save-HostSettings {
  try {
    if (-not (Test-Path -LiteralPath $StateDir)) { New-Item -ItemType Directory -Path $StateDir -Force | Out-Null }
    ($script:Settings | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $SettingsFile -Encoding UTF8
  } catch {
    Add-HostError "保存设置失败：$($_.Exception.Message)"
  }
}

function Publish-PlaybackState {
  <# 把宿主侧状态回传给页面，让页面 UI 与真实窗口状态保持一致 #>
  Set-HostState @{
    mode       = $script:Settings.mode
    layer      = $script:Settings.layer
    selectable = $script:Settings.selectable
    autoStart  = $script:Settings.autoStart
  }
}

# ------------------------------------------------------------------ 系统 DPI
function Get-SystemDpi {
  try {
    Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
    $g = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero)
    $dpi = $g.DpiX
    $g.Dispose()
    if ($dpi -gt 0) { return [int]$dpi }
  } catch { }
  return 96
}

# ------------------------------------------------------------------ 应用窗口状态
function Apply-Layer {
  param([string]$Layer, [switch]$Quiet)
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  $r = Set-WindowLayer -Hwnd $script:Hwnd -Layer $Layer
  if (-not $r.ok) { Add-HostError "设置窗口层级失败：$($r.error)"; return }
  $script:Settings.layer = $Layer
  if (-not $Quiet) { Save-HostSettings; Publish-PlaybackState }
}

function Apply-Selectable {
  param([bool]$Selectable, [switch]$Quiet)
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  $r = Set-WindowSelectable -Hwnd $script:Hwnd -Selectable $Selectable
  if (-not $r.ok) { Add-HostError "设置穿透状态失败：$($r.error)"; return }
  $script:Settings.selectable = $Selectable
  if (-not $Quiet) { Save-HostSettings; Publish-PlaybackState }

  $tip = if ($Selectable) { '待办清单 · 可选中' } else { '待办清单 · 不可选中（按 ' + $script:Settings.hotkeySelectable + ' 恢复）' }
  Update-TrayTooltip -Tooltip $tip
}

function Apply-Geometry {
  param($Geometry, [int]$Dpr = 1)
  if ($script:Hwnd -eq [IntPtr]::Zero -or -not $Geometry) { return }

  $wa = Get-PrimaryWorkArea
  $scale = if ($Dpr -gt 0) { $Dpr } else { (Get-SystemDpi) / 96.0 }

  $w = if ($Geometry.w) { [int][Math]::Round([double]$Geometry.w * $scale) } else { [int][Math]::Round(380 * $scale) }
  $h = if ($Geometry.h) { [int][Math]::Round([double]$Geometry.h * $scale) } else { [int][Math]::Round(520 * $scale) }

  $x = $Geometry.x
  $y = $Geometry.y
  if ($null -eq $x -or $null -eq $y) {
    # 首次运行：停靠右下角，留 16px 边距（浮窗的默认位置）
    $margin = [int][Math]::Round(16 * $scale)
    $x = $wa.x + $wa.w - $w - $margin
    $y = $wa.y + $wa.h - $h - $margin
  } else {
    $x = [int][Math]::Round([double]$x * $scale)
    $y = [int][Math]::Round([double]$y * $scale)
  }

  # 防止重启后窗口落在屏幕外而"找不回来"（FR-75 / 风险 R8）
  $candidate = [pscustomobject]@{ x = $x; y = $y; w = $w; h = $h }
  if (-not (Test-BoundsOnScreen -Bounds $candidate -WorkArea $wa)) {
    $margin = [int][Math]::Round(16 * $scale)
    $x = $wa.x + $wa.w - $w - $margin
    $y = $wa.y + $wa.h - $h - $margin
  }

  [void][DesktopTodo.Win32]::MoveWindow($script:Hwnd, [int]$x, [int]$y, [int]$w, [int]$h, $true)
  Set-HostBounds -Bounds (Get-WindowBounds -Hwnd $script:Hwnd) -Hwnd $script:Hwnd
}

function Publish-Bounds {
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  Set-HostBounds -Bounds (Get-WindowBounds -Hwnd $script:Hwnd) -Hwnd $script:Hwnd
}

# ------------------------------------------------------------------ 窗口显示/隐藏
function Show-MainWindow {
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  if ([DesktopTodo.Win32]::IsIconic($script:Hwnd)) {
    [void][DesktopTodo.Win32]::ShowWindow($script:Hwnd, [DesktopTodo.Win32]::SW_RESTORE)
  } else {
    [void][DesktopTodo.Win32]::ShowWindow($script:Hwnd, [DesktopTodo.Win32]::SW_SHOWNORMAL)
  }
  [void][DesktopTodo.Win32]::SetForegroundWindow($script:Hwnd)
  # 置底状态下显示后会被压在下面，重新应用层级让用户真的看得见
  if ($script:Settings.layer -ne 'normal') { Apply-Layer -Layer $script:Settings.layer -Quiet }
  Publish-Bounds
}

function Hide-MainWindow {
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  Hide-Window -Hwnd $script:Hwnd
}

function Toggle-MainWindow {
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  if ([DesktopTodo.Win32]::IsWindowVisible($script:Hwnd) -and -not [DesktopTodo.Win32]::IsIconic($script:Hwnd)) {
    Hide-MainWindow
  } else {
    Show-MainWindow
  }
}

# ------------------------------------------------------------------ 托盘
function Get-TrayMenuItems {
  $pending = if ($script:PendingCount -ne $null) { $script:PendingCount } else { 0 }
  return @(
    @{ id = 100; text = '显示 / 隐藏窗口' }
    @{ id = 0;   separator = $true }
    @{ id = 101; text = '窗口置顶';     checked = ($script:Settings.layer -eq 'top') }
    @{ id = 102; text = '窗口置底';     checked = ($script:Settings.layer -eq 'bottom') }
    @{ id = 0;   separator = $true }
    @{ id = 103; text = '允许交互（取消鼠标穿透）'; checked = $script:Settings.selectable }
    @{ id = 104; text = '开机自启';     checked = $script:Settings.autoStart }
    @{ id = 0;   separator = $true }
    @{ id = 105; text = "待办 $pending 项（点击显示）" }
    @{ id = 0;   separator = $true }
    @{ id = 106; text = '退出' }
  )
}

function Invoke-TrayCommand {
  param([int]$Id)

  switch ($Id) {
    100 { Toggle-MainWindow }
    105 { Show-MainWindow }
    101 {
      $next = if ($script:Settings.layer -eq 'top') { 'normal' } else { 'top' }
      Apply-Layer -Layer $next
    }
    102 {
      $next = if ($script:Settings.layer -eq 'bottom') { 'normal' } else { 'bottom' }
      Apply-Layer -Layer $next
    }
    103 {
      # 逃生通道：即使用户已经点了穿透进不去窗口，也能从这里恢复
      $next = -not $script:Settings.selectable
      Apply-Selectable -Selectable $next
      if ($next) { Show-TrayBalloon -Title '待办清单' -Text '已恢复鼠标交互' }
    }
    104 {
      $next = -not $script:Settings.autoStart
      $r = Set-AutoStart -Enabled $next -LauncherPath (Join-Path $HostDir 'launcher.ps1')
      if ($r.ok) {
        $script:Settings.autoStart = $next
        Save-HostSettings
        Publish-PlaybackState
        $msg = if ($next) { '已开启开机自启' } else { '已关闭开机自启' }
        Show-TrayBalloon -Title '待办清单' -Text $msg
      } else {
        Add-HostError "开机自启设置失败：$($r.error)"
        Show-TrayBalloon -Title '开机自启设置失败' -Text $r.error -Flags 0x00000003
      }
    }
    106 { $script:Running = $false }
  }
}

# ------------------------------------------------------------------ 热键
function Register-Hotkeys {
  <# 两个热键分别独立注册：一个失败不影响另一个，且失败原因必须让用户知道 #>
  if ($script:HotkeyShowOk) { [void][DesktopTodo.Win32]::UnregisterHotKey($script:MsgHwnd, $script:HotkeyShow) }
  if ($script:HotkeySelOk)  { [void][DesktopTodo.Win32]::UnregisterHotKey($script:MsgHwnd, $script:HotkeySel) }
  $script:HotkeyShowOk = $false
  $script:HotkeySelOk  = $false

  $spec = ConvertTo-HotkeySpec -Text $script:Settings.hotkey
  if ($spec) {
    $script:HotkeyShowOk = [DesktopTodo.Win32]::RegisterHotKey($script:MsgHwnd, $script:HotkeyShow, $spec.modifiers, $spec.vk)
    if (-not $script:HotkeyShowOk) {
      Add-HostError "全局快捷键 $($script:Settings.hotkey) 注册失败（可能被其他程序占用，如 Windows Terminal）。窗口仍可通过托盘菜单显示/隐藏。"
    }
  } else {
    Add-HostError "无法解析快捷键配置：$($script:Settings.hotkey)"
  }

  $spec2 = ConvertTo-HotkeySpec -Text $script:Settings.hotkeySelectable
  if ($spec2) {
    $script:HotkeySelOk = [DesktopTodo.Win32]::RegisterHotKey($script:MsgHwnd, $script:HotkeySel, $spec2.modifiers, $spec2.vk)
    if (-not $script:HotkeySelOk) {
      # 这是穿透的唯一键盘逃生通道，失败必须显式告警（FR-77）
      Add-HostError "穿透恢复快捷键 $($script:Settings.hotkeySelectable) 注册失败。请改用托盘菜单「允许交互」恢复鼠标操作。"
      Show-TrayBalloon -Title '快捷键注册失败' `
        -Text "无法注册 $($script:Settings.hotkeySelectable)，请改用托盘菜单「允许交互」。" -Flags 0x00000002
    }
  }

  if ($script:HotkeySelOk) {
    # 穿透状态下唯一可靠的键盘逃生通道，用气泡明确告知用户
    Show-TrayBalloon -Title '待办清单已启动' `
      -Text "Ctrl+Alt+T 显示/隐藏 · $($script:Settings.hotkeySelectable) 切换鼠标穿透" -Flags 0x00000001
  }
}

function Unregister-Hotkeys {
  try {
    if ($script:HotkeyShowOk) { [void][DesktopTodo.Win32]::UnregisterHotKey($script:MsgHwnd, $script:HotkeyShow); $script:HotkeyShowOk = $false }
    if ($script:HotkeySelOk)  { [void][DesktopTodo.Win32]::UnregisterHotKey($script:MsgHwnd, $script:HotkeySel);  $script:HotkeySelOk = $false }
  } catch { }
}

# ------------------------------------------------------------------ 指令处理
function Invoke-PageCommand {
  param([Parameter(Mandatory)]$Item)

  $cmd = [string]$Item.cmd
  $p = $Item.payload
  $needSave = $false

  switch ($cmd) {
    'hello' {
      if ($p) {
        if ($p.mode)     { $script:Settings.mode = [string]$p.mode }
        if ($p.layer)    { $script:Settings.layer = [string]$p.layer }
        if ($null -ne $p.selectable) { $script:Settings.selectable = [bool]$p.selectable }
        if ($null -ne $p.autoStart)  { $script:Settings.autoStart = [bool]$p.autoStart }
        if ($null -ne $p.startMinimized) { $script:Settings.startMinimized = [bool]$p.startMinimized }
        if ($p.hotkey)   { $script:Settings.hotkey = [string]$p.hotkey }
        if ($p.hotkeySelectable) { $script:Settings.hotkeySelectable = [string]$p.hotkeySelectable }
        if ($p.dpr)      { $script:Settings.dpr = [double]$p.dpr }
        if ($p.geometry) { $script:Settings.geometry = $p.geometry }

        Apply-Layer -Layer $script:Settings.layer -Quiet
        Apply-Selectable -Selectable $script:Settings.selectable -Quiet
        Apply-Geometry -Geometry $p.geometry -Dpr ([int]$script:Settings.dpr)

        # 快捷键可能被页面改过，重新注册（PS 5.1 不支持 ?? 运算符，故显式判断）
        Register-Hotkeys
      }
      Save-HostSettings
      Publish-PlaybackState
      Publish-Bounds
    }
    'set-layer' {
      if ($p -and $p.layer) { Apply-Layer -Layer ([string]$p.layer) }
    }
    'set-selectable' {
      if ($p) { Apply-Selectable -Selectable ([bool]$p.selectable) }
    }
    'set-mode' {
      if ($p -and $p.mode) {
        $script:Settings.mode = [string]$p.mode
        $needSave = $true
        Publish-PlaybackState
      }
    }
    'set-autostart' {
      if ($p) {
        # 注册表里要写的是 host 子目录下的启动脚本
        $r = Set-AutoStart -Enabled ([bool]$p.enabled) -LauncherPath (Join-Path $HostDir 'launcher.ps1')
        if ($r.ok) {
          $script:Settings.autoStart = [bool]$p.enabled
          $needSave = $true
          Publish-PlaybackState
        } else {
          Add-HostError "开机自启设置失败：$($r.error)"
          Set-HostState @{ autoStart = $script:Settings.autoStart; lastError = $r.error }
        }
      }
    }
    'set-start-minimized' {
      if ($p) { $script:Settings.startMinimized = [bool]$p.enabled; $needSave = $true; Publish-PlaybackState }
    }
    'move-by' {
      if ($p) {
        $dpr = if ($p.dpr) { [double]$p.dpr } else { 1 }
        $r = Move-WindowBy -Hwnd $script:Hwnd -Dx ([double]$p.dx) -Dy ([double]$p.dy) -Dpr $dpr
        if ($r.ok) { Set-HostBounds -Bounds $r.bounds; Mark-GeometryDirty } else { Add-HostError $r.error }
      }
    }
    'resize-by' {
      if ($p) {
        $dpr = if ($p.dpr) { [double]$p.dpr } else { 1 }
        $r = Resize-WindowBy -Hwnd $script:Hwnd -Dw ([double]$p.dw) -Dh ([double]$p.dh) -Dpr $dpr
        if ($r.ok) { Set-HostBounds -Bounds $r.bounds; Mark-GeometryDirty } else { Add-HostError $r.error }
      }
    }
    'commit-geometry' {
      Flush-Geometry
    }
    'counts' {
      if ($p) {
        $script:PendingCount = [int]$p.pending
        $overdue = if ($p.overdue) { [int]$p.overdue } else { 0 }
        $tip = '待办清单 · ' + $script:PendingCount + ' 项待办'
        if ($overdue -gt 0) { $tip += ' · ' + $overdue + ' 项逾期' }
        Update-TrayTooltip -Tooltip $tip
      }
    }
    'notify' {
      if ($p) {
        $title = if ($p.title) { [string]$p.title } else { '待办提醒' }
        $body = if ($p.body) { [string]$p.body } else { '有任务需要处理' }
        Show-TrayBalloon -Title $title -Text $body
      }
    }
    'minimize' { Minimize-Window -Hwnd $script:Hwnd }
    'hide'     { Hide-MainWindow }
    'show'     { Show-MainWindow }
    'toggle-visible' { Toggle-MainWindow }
    'close'    { $script:Running = $false }
    'refresh' {
      Apply-Layer -Layer $script:Settings.layer -Quiet
      Apply-Selectable -Selectable $script:Settings.selectable -Quiet
      Publish-PlaybackState
      Publish-Bounds
    }
    default {
      Add-HostError "未知指令：$cmd"
    }
  }

  if ($needSave) { Save-HostSettings }
  Publish-HostResponse
}

function Mark-GeometryDirty {
  # 拖拽过程中每帧都写盘会造成大量 I/O，延迟 700ms 合并写入。
  # 注意：System.Timers.Timer 没有 Start()/Stop() 方法（那是 Forms.Timer 与
  # Diagnostics.Stopwatch 的 API），重置计时要用 Enabled 属性。
  $script:GeometryDirty = $true
  if ($script:GeometryTimer) {
    $script:GeometryTimer.Enabled = $false
    $script:GeometryTimer.Enabled = $true
  }
}

function Flush-Geometry {
  if ($script:Hwnd -eq [IntPtr]::Zero) { return }
  $b = Get-WindowBounds -Hwnd $script:Hwnd
  if ($b) {
    $dpr = if ($script:Settings.dpr) { [double]$script:Settings.dpr } else { 1 }
    $script:Settings.geometry = @{
      x = [int][Math]::Round($b.x / $dpr)
      y = [int][Math]::Round($b.y / $dpr)
      w = [int][Math]::Round($b.w / $dpr)
      h = [int][Math]::Round($b.h / $dpr)
    }
    Set-HostBounds -Bounds $b
    Save-HostSettings
  }
  $script:GeometryDirty = $false
}

# ------------------------------------------------------------------ 窗口消息
function Initialize-WindowMessages {
  $script:OnWindowMessage = {
    param([IntPtr]$hWnd, [uint32]$msg, [IntPtr]$wParam, [IntPtr]$lParam)

    switch ($msg) {
      ([DesktopTodo.Win32]::WM_HOTKEY) {
        $id = [int]$wParam
        if ($id -eq $script:HotkeyShow) { Toggle-MainWindow; return $true }
        if ($id -eq $script:HotkeySel) {
          $next = -not $script:Settings.selectable
          Apply-Selectable -Selectable $next
          if ($next) { Show-TrayBalloon -Title '待办清单' -Text '已恢复鼠标交互' }
          return $true
        }
        return $false
      }

      ([DesktopTodo.Win32]::WM_TRAYICON) {
        # NOTIFYICON_VERSION_4 下 lParam 低字为消息、高字为图标 id
        $mouseMsg = [int]($lParam.ToInt64() -band 0xFFFF)
        if ($mouseMsg -eq [int][DesktopTodo.Win32]::WM_LBUTTONUP) {
          Toggle-MainWindow
          return $true
        }
        if ($mouseMsg -eq [int][DesktopTodo.Win32]::WM_RBUTTONUP) {
          $cmd = Show-TrayMenu -Hwnd $hWnd -Items (Get-TrayMenuItems)
          if ($cmd -gt 0) { Invoke-TrayCommand -Id $cmd }
          return $true
        }
        return $false
      }

      ([DesktopTodo.Win32]::WM_CLOSE) {
        $script:Running = $false
        return $true
      }

      default { return $false }
    }
  }
}

# ------------------------------------------------------------------ 停止/切换已有实例
function Send-SignalToRunning {
  <# 用命名事件通知已运行实例；比写临时文件更可靠 #>
  param([Parameter(Mandatory)][string]$Name)
  try {
    $ev = [System.Threading.EventWaitHandle]::OpenExisting($Name)
    [void]$ev.Set()
    $ev.Dispose()
    return $true
  } catch {
    return $false
  }
}

# =============================================================================
# 主流程
# =============================================================================
try {
  if (-not (Test-Path -LiteralPath $StateDir)) { New-Item -ItemType Directory -Path $StateDir -Force | Out-Null }

  # ---- 单实例 ----
  $createdNew = $false
  $script:Mutex = New-Object System.Threading.Mutex($true, $script:MutexName, [ref]$createdNew)

  if (-not $createdNew) {
    if ($Stop) {
      if (Send-SignalToRunning -Name 'Global\DesktopTodoList.Stop') { Write-Host '已请求停止现有实例。' }
      else { Write-Host '未找到正在运行的实例。' }
      exit 0
    }
    if ($Toggle) {
      if (Send-SignalToRunning -Name 'Global\DesktopTodoList.Toggle') { Write-Host '已请求切换显示状态。' }
      else { Write-Host '未找到正在运行的实例。' }
      exit 0
    }
    # 普通重复启动 = 把已有窗口显示出来（符合用户直觉）
    if (Send-SignalToRunning -Name 'Global\DesktopTodoList.Toggle') { exit 0 }
    Write-Host '另一个实例正在运行，但无法唤出窗口。'
    exit 0
  }

  Write-Log '宿主启动'

  # ---- 加载模块 ----
  . (Join-Path $HostDir 'window.ps1')
  . (Join-Path $HostDir 'server.ps1')

  $script:Settings = Read-HostSettings
  $script:PendingCount = 0
  $script:Settings.dpr = (Get-SystemDpi) / 96.0

  # ---- 启动静态服务器（同时是 keepalive）----
  $srv = Start-StaticServer -Root $SiteRoot -Port $Port
  if (-not $srv.ok) {
    Write-Log "静态服务器启动失败：$($srv.error)" 'ERROR'
    Write-Host ''
    Write-Host '无法启动本地服务，桌面模式不可用。' -ForegroundColor Red
    Write-Host $srv.error -ForegroundColor Red
    Write-Host '可直接双击 src\index.html 使用浏览器模式（浮窗/置顶置底/穿透/自启不可用）。'
    exit 1
  }
  Write-Log "静态服务器已启动：$($srv.url)"

  # 发起首次异步接受连接：非阻塞消息循环依赖它
  if (-not (Start-ServerAsyncWait)) {
    Write-Log '无法开始接受连接。' 'ERROR'
  }

  # ---- 探测 Edge ----
  $edge = Get-EdgePath
  if (-not $edge) {
    Write-Log '未找到 Microsoft Edge，无法以浮窗方式启动。' 'ERROR'
    Show-HostErrorHint
    exit 1
  }
  Write-Log "使用 Edge：$edge"

  # ---- 创建消息窗口与托盘 ----
  $script:MsgHwnd = New-MessageWindow
  Initialize-WindowMessages

  $tray = New-TrayIcon -Hwnd $script:MsgHwnd -Tooltip '待办清单' -IconSource $edge
  $script:TrayReady = $tray.ok
  if (-not $tray.ok) { Add-HostError $tray.error }

  # ---- 报告宿主能力（页面据此决定哪些设置可用）----
  Set-HostCaps @{
    topmost      = $true
    bottom       = $true
    clickThrough = $true
    autostart    = $true
    tray         = $script:TrayReady
    floating     = $true
  }

  # ---- 启动 Edge（--app 模式 = 无地址栏的独立窗口）----
  $url = '{0}?host=1&p={1}&t={2}' -f $srv.url, $srv.port, $srv.token
  if (-not (Test-Path -LiteralPath $ProfileDir)) { New-Item -ItemType Directory -Path $ProfileDir -Force | Out-Null }

  $edgeArgs = @(
    "--app=$url"
    "--user-data-dir=$ProfileDir"
    '--no-first-run'
    '--no-default-browser-check'
    '--disable-features=Translate,msEdgeTranslate'
  )

  Write-Log '正在启动 Edge 窗口…'
  $script:EdgeProc = Start-Process -FilePath $edge -ArgumentList $edgeArgs -PassThru

  $script:Hwnd = Get-ProcessMainWindowHandle -TitleLike '*待办清单*' -TimeoutSeconds 30
  if ($script:Hwnd -eq [IntPtr]::Zero) {
    # 标题匹配失败时退一步：按启动后的新窗口找
    $script:Hwnd = Get-ProcessMainWindowHandle -TitleLike '*Todo*' -TimeoutSeconds 5
  }

  if ($script:Hwnd -eq [IntPtr]::Zero) {
    Write-Log '未能定位到 Edge 窗口。' 'ERROR'
    Add-HostError '未能定位到 Edge 窗口，窗口控制功能不可用。'
    Show-HostErrorHint
    # 仍然保持服务器与托盘存活，让用户至少能用托盘退出
    Set-HostCaps @{ topmost = $false; bottom = $false; clickThrough = $false; autostart = $true; tray = $script:TrayReady }
  } else {
    Write-Log ("已获取窗口句柄：0x{0:X}" -f $script:Hwnd.ToInt64())

    Apply-Geometry -Geometry $script:Settings.geometry -Dpr $script:Settings.dpr
    Apply-Layer -Layer $script:Settings.layer -Quiet
    Apply-Selectable -Selectable $script:Settings.selectable -Quiet
    Publish-Bounds

    # 开机自启且配置为最小化时，直接隐藏，静默驻留托盘
    if ($AutoStart -and $script:Settings.startMinimized) {
      Hide-MainWindow
      Write-Log '按设置以最小化方式启动'
    }
  }

  Register-Hotkeys

  # ---- 几何写盘节流定时器 ----
  $script:GeometryTimer = New-Object System.Timers.Timer 700
  $script:GeometryTimer.AutoReset = $false
  $script:GeometryTimer.add_Elapsed({ if ($script:GeometryDirty) { Flush-Geometry } })

  # ---- 供 -Stop / -Toggle 使用的命名事件 ----
  $stopEvent   = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::AutoReset, 'Global\DesktopTodoList.Stop')
  $toggleEvent = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::AutoReset, 'Global\DesktopTodoList.Toggle')
  $waitHandles = @($stopEvent, $toggleEvent)

  Write-Log '宿主就绪，进入消息循环'

  # ---- 主循环：消息 + HTTP + 事件，三合一 ----
  # GetMessage 带 100ms 超时窗口：既能及时处理窗口消息，又能在空转时腾出时间
  # 处理 HTTP 请求与命名事件（GetMessage 会阻塞，故不能完全依赖它驱动一切）
  $msg = New-Object DesktopTodo.MSG
  while ($script:Running) {

    # 1) 排空窗口消息
    while ([DesktopTodo.Win32]::PeekMessage([ref]$msg, [IntPtr]::Zero, 0, 0, 1)) {   # PM_REMOVE
      if ($msg.message -eq [DesktopTodo.Win32]::WM_QUIT) { $script:Running = $false; break }
      [void][DesktopTodo.Win32]::TranslateMessage([ref]$msg)
      [void][DesktopTodo.Win32]::DispatchMessage([ref]$msg)
    }
    if (-not $script:Running) { break }

    # 2) 处理 HTTP（非阻塞：ReceiveTimeout 很短）
    for ($i = 0; $i -lt 8; $i++) {
      if (-not (Invoke-ServerStep)) { $script:Running = $false; break }
    }
    if (-not $script:Running) { break }

    # 3) 消费页面指令
    $cmds = Receive-HostCommand
    foreach ($c in $cmds) { Invoke-PageCommand -Item $c }

    # 4) 外部信号
    $idx = [System.Threading.WaitHandle]::WaitAny($waitHandles, 0)
    if ($idx -eq 0) { Write-Log '收到停止信号'; break }
    if ($idx -eq 1) { Toggle-MainWindow }

    # 5) Edge 已退出则宿主一并退出，避免留下孤儿进程
    if ($script:EdgeProc -and $script:EdgeProc.HasExited) {
      Write-Log 'Edge 进程已退出，宿主一并退出'
      break
    }

    Start-Sleep -Milliseconds 25
  }

  Write-Log '宿主退出中…'
}
catch {
  Write-Log "宿主异常终止：$($_.Exception.Message)`n$($_.ScriptStackTrace)" 'ERROR'
}
finally {
  try { Flush-Geometry } catch { }
  try { Unregister-Hotkeys } catch { }
  try { Remove-TrayIcon } catch { }
  try { Stop-StaticServer } catch { }
  try {
    if ($script:MsgHwnd -ne [IntPtr]::Zero) { [void][DesktopTodo.Win32]::DestroyWindow($script:MsgHwnd) }
  } catch { }
  try {
    # 关掉我们启动的 Edge 窗口，避免残留
    if ($script:Hwnd -ne [IntPtr]::Zero) { Close-WindowGracefully -Hwnd $script:Hwnd }
  } catch { }
  try { if ($script:Mutex) { $script:Mutex.ReleaseMutex(); $script:Mutex.Dispose() } } catch { }
  Write-Log '宿主已退出'
}

function Show-HostErrorHint {
  Write-Host ''
  Write-Host '桌面模式启动失败。你可以：' -ForegroundColor Yellow
  Write-Host '  1. 直接双击 src\index.html 使用浏览器模式（任务功能完整）'
  Write-Host '  2. 查看日志：' -NoNewline; Write-Host $LogFile
  Write-Host '  3. 确认已安装 Microsoft Edge（Windows 10/11 通常自带）'
  Write-Host ''
  Read-Host '按回车键关闭'
}
