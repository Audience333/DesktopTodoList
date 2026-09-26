# =============================================================================
# window.ps1 — Win32 窗口控制模块
# -----------------------------------------------------------------------------
# 需求对应：FR-72 置顶 / FR-73 置底 / FR-74 窗口穿透 / FR-75 位置记忆
#           FR-77 穿透逃生机制（热键 + 托盘菜单）/ FR-52 托盘 / FR-70 开机自启
#
# 设计说明：
#   * 全部使用 P/Invoke（Add-Type）直接调用 user32/shell32，不依赖任何
#     NuGet 包或 WebView2 SDK，因此无需安装运行时（见需求文档 §11.2）
#   * 托盘与全局热键都通过一个"消息专用窗口"接收，用 GetMessage 消息循环驱动，
#     避免依赖 WinForms（PowerShell 5.1 默认 MTA，WinForms 需要 STA）
# =============================================================================

Set-StrictMode -Version Latest

# ----------------------------------------------------------------- P/Invoke
if (-not ('DesktopTodo.Win32' -as [type])) {
Add-Type -Language CSharp @'
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace DesktopTodo {

  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int Left, Top, Right, Bottom; }

  [StructLayout(LayoutKind.Sequential)]
  public struct POINT { public int X, Y; }

  [StructLayout(LayoutKind.Sequential)]
  public struct NOTIFYICONDATA {
    public uint   cbSize;
    public IntPtr hWnd;
    public uint   uID;
    public uint   uFlags;
    public uint   uCallbackMessage;
    public IntPtr hIcon;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
    public string szTip;
    public uint   dwState;
    public uint   dwStateMask;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string szInfo;
    public uint   uTimeoutOrVersion;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string szInfoTitle;
    public uint   dwInfoFlags;
    public Guid   guidItem;
    public IntPtr hBalloonIcon;
  }

  [StructLayout(LayoutKind.Sequential)]
  public struct WNDCLASSEX {
    public uint   cbSize;
    public uint   style;
    public IntPtr lpfnWndProc;
    public int    cbClsExtra;
    public int    cbWndExtra;
    public IntPtr hInstance;
    public IntPtr hIcon;
    public IntPtr hCursor;
    public IntPtr hbrBackground;
    public string lpszMenuName;
    public string lpszClassName;
    public IntPtr hIconSm;
  }

  public delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

  public static class Win32 {
    // ---- 窗口层级 ----
    public static readonly IntPtr HWND_TOPMOST   = new IntPtr(-1);
    public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    public static readonly IntPtr HWND_TOP       = new IntPtr(0);
    public static readonly IntPtr HWND_BOTTOM    = new IntPtr(1);

    public const uint SWP_NOSIZE       = 0x0001;
    public const uint SWP_NOMOVE       = 0x0002;
    public const uint SWP_NOZORDER     = 0x0004;
    public const uint SWP_NOACTIVATE   = 0x0010;
    public const uint SWP_FRAMECHANGED = 0x0020;
    public const uint SWP_SHOWWINDOW   = 0x0040;

    // ---- 扩展样式 ----
    public const int  GWL_EXSTYLE     = -20;
    public const int  WS_EX_TRANSPARENT = 0x00000020;
    public const int  WS_EX_LAYERED     = 0x00080000;
    public const int  WS_EX_NOACTIVATE  = 0x08000000;
    public const int  WS_EX_TOOLWINDOW  = 0x00000080;

    // ---- ShowWindow ----
    public const int SW_HIDE     = 0;
    public const int SW_SHOWNORMAL = 1;
    public const int SW_SHOWNOACTIVATE = 4;
    public const int SW_MINIMIZE = 6;
    public const int SW_RESTORE  = 9;

    // ---- 消息 ----
    public const uint WM_DESTROY     = 0x0002;
    public const uint WM_CLOSE       = 0x0010;
    public const uint WM_COMMAND     = 0x0111;
    public const uint WM_HOTKEY      = 0x0312;
    public const uint WM_APP         = 0x8000;
    public const uint WM_LBUTTONUP   = 0x0202;
    public const uint WM_RBUTTONUP   = 0x0205;
    public const uint WM_CONTEXTMENU = 0x007B;
    public const uint WM_TRAYICON    = WM_APP + 1;

    // ---- 热键修饰键 ----
    public const uint MOD_ALT      = 0x0001;
    public const uint MOD_CONTROL  = 0x0002;
    public const uint MOD_SHIFT    = 0x0004;
    public const uint MOD_NOREPEAT = 0x4000;

    // ---- 托盘 ----
    public const uint NIM_ADD        = 0x00000000;
    public const uint NIM_MODIFY     = 0x00000001;
    public const uint NIM_DELETE     = 0x00000002;
    public const uint NIM_SETVERSION = 0x00000004;
    public const uint NOTIFYICON_VERSION_4 = 4;

    public const uint NIF_MESSAGE = 0x00000001;
    public const uint NIF_ICON    = 0x00000002;
    public const uint NIF_TIP     = 0x00000004;
    public const uint NIF_INFO    = 0x00000010;

    public const uint NIIF_INFO    = 0x00000001;
    public const uint NIIF_WARNING = 0x00000002;
    public const uint NIIF_ERROR   = 0x00000003;

    // ---- 菜单 ----
    public const uint MF_STRING    = 0x00000000;
    public const uint MF_SEPARATOR = 0x00000800;
    public const uint MF_CHECKED   = 0x00000008;
    public const uint MF_UNCHECKED = 0x00000000;
    public const uint TPM_RIGHTBUTTON = 0x0002;
    public const uint TPM_RETURNCMD   = 0x0100;
    public const uint TPM_NONOTIFY    = 0x0080;

    // ---- DPI ----
    public const int GWL_STYLE = -16;

    // ================================ user32 ================================
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter,
      int X, int Y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtr", SetLastError = true)]
    private static extern IntPtr GetWindowLongPtr64(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll", EntryPoint = "GetWindowLong", SetLastError = true)]
    private static extern IntPtr GetWindowLong32(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtr", SetLastError = true)]
    private static extern IntPtr SetWindowLongPtr64(IntPtr hWnd, int nIndex, IntPtr dwNewLong);
    [DllImport("user32.dll", EntryPoint = "SetWindowLong", SetLastError = true)]
    private static extern IntPtr SetWindowLong32(IntPtr hWnd, int nIndex, IntPtr dwNewLong);

    public static IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex) {
      return IntPtr.Size == 8 ? GetWindowLongPtr64(hWnd, nIndex) : GetWindowLong32(hWnd, nIndex);
    }
    public static IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong) {
      return IntPtr.Size == 8 ? SetWindowLongPtr64(hWnd, nIndex, dwNewLong)
                              : SetWindowLong32(hWnd, nIndex, dwNewLong);
    }

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    // ---- 热键 ----
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    // ---- 消息循环 ----
    [DllImport("user32.dll", SetLastError = true)]
    public static extern int GetMessage(out MSG lpMsg, IntPtr hWnd, uint wMsgFilterMin, uint wMsgFilterMax);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern bool PeekMessage(out MSG lpMsg, IntPtr hWnd,
      uint wMsgFilterMin, uint wMsgFilterMax, uint wRemoveMsg);

    [DllImport("user32.dll")]
    public static extern bool TranslateMessage(ref MSG lpMsg);

    [DllImport("user32.dll")]
    public static extern IntPtr DispatchMessage(ref MSG lpMsg);

    [DllImport("user32.dll")]
    public static extern bool PostQuitMessage(int nExitCode);

    // PM_REMOVE：取走消息
    public const uint PM_REMOVE = 0x0001;

    [StructLayout(LayoutKind.Sequential)]
    public struct MSG {
      public IntPtr hwnd;
      public uint   message;
      public IntPtr wParam;
      public IntPtr lParam;
      public uint   time;
      public POINT  pt;
    }

    // ---- 窗口类与消息专用窗口 ----
    [DllImport("user32.dll", SetLastError = true)]
    public static extern ushort RegisterClassEx(ref WNDCLASSEX lpwcx);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr CreateWindowEx(uint dwExStyle, string lpClassName, string lpWindowName,
      uint dwStyle, int x, int y, int nWidth, int nHeight, IntPtr hWndParent, IntPtr hMenu,
      IntPtr hInstance, IntPtr lpParam);

    [DllImport("user32.dll")]
    public static extern IntPtr DefWindowProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool DestroyWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr LoadCursor(IntPtr hInstance, int lpCursorName);

    [DllImport("kernel32.dll")]
    public static extern IntPtr GetModuleHandle(string lpModuleName);

    // ---- 菜单 ----
    [DllImport("user32.dll")]
    public static extern IntPtr CreatePopupMenu();

    [DllImport("user32.dll")]
    public static extern bool DestroyMenu(IntPtr hMenu);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool AppendMenu(IntPtr hMenu, uint uFlags, IntPtr uIDNewItem, string lpNewItem);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern int TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y,
      int nReserved, IntPtr hWnd, IntPtr prcRect);

    [DllImport("user32.dll")]
    public static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    public static extern bool SetMenuDefaultItem(IntPtr hMenu, uint uItem, uint fByPos);

    // ---- DPI ----
    [DllImport("shcore.dll")]
    public static extern int SetProcessDpiAwareness(int value);

    [DllImport("user32.dll")]
    public static extern bool SetProcessDPIAware();

    // ================================ shell32 ===============================
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern bool Shell_NotifyIcon(uint dwMessage, ref NOTIFYICONDATA lpData);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern uint ExtractIconEx(string lpszFile, int nIconIndex,
      IntPtr[] phiconLarge, IntPtr[] phiconSmall, uint nIcons);
  }
}
'@
}

# 常量别名，方便脚本侧使用
$script:W = [DesktopTodo.Win32]

# ------------------------------------------------ 进程级 DPI 感知（尽早调用）
try {
  # PROCESS_PER_MONITOR_DPI_AWARE = 2；失败说明已设置过或系统不支持，均无需处理
  [void][DesktopTodo.Win32]::SetProcessDpiAwareness(2)
} catch {
  try { [void][DesktopTodo.Win32]::SetProcessDPIAware() } catch { }
}

# ============================================================== 窗口层级

function Set-WindowLayer {
  <#
    .SYNOPSIS
      设置窗口层级：top（置顶）/ normal（普通）/ bottom（置底）
    .NOTES
      置顶与置底互斥，同时只会生效一个（FR-79）
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [Parameter(Mandatory)][ValidateSet('top', 'normal', 'bottom')][string]$Layer
  )

  if (-not [DesktopTodo.Win32]::IsWindow($Hwnd)) {
    return @{ ok = $false; error = '窗口句柄无效' }
  }

  $insertAfter = switch ($Layer) {
    'top'    { [DesktopTodo.Win32]::HWND_TOPMOST }
    'bottom' { [DesktopTodo.Win32]::HWND_BOTTOM }
    default  { [DesktopTodo.Win32]::HWND_NOTOPMOST }
  }

  $flags = [DesktopTodo.Win32]::SWP_NOMOVE -bor [DesktopTodo.Win32]::SWP_NOSIZE -bor `
           [DesktopTodo.Win32]::SWP_NOACTIVATE

  # TopMost 与 Bottom 的语义不同：TopMost 需要 Z 序恒定，不能用 NOZORDER
  $ok = [DesktopTodo.Win32]::SetWindowPos($Hwnd, $insertAfter, 0, 0, 0, 0, $flags)

  if (-not $ok) {
    return @{ ok = $false; error = "SetWindowPos 失败，Win32 错误码 $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
  }
  return @{ ok = $true; layer = $Layer }
}

# ============================================================== 鼠标穿透

function Set-WindowSelectable {
  <#
    .SYNOPSIS
      切换窗口是否可被鼠标交互。
    .DESCRIPTION
      $Selectable = $false 时设置 WS_EX_TRANSPARENT | WS_EX_LAYERED（并叠加
      WS_EX_NOACTIVATE），使鼠标事件穿透到下层窗口，且点击不抢焦点。
      $Selectable = $true 时清除这些样式，恢复正常交互。
    .NOTES
      改样式后必须调用 SetWindowPos(SWP_FRAMECHANGED) 才会生效。
      FR-77：进入穿透后窗口无法再被点击，恢复只能依赖全局热键或托盘菜单。
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [Parameter(Mandatory)][bool]$Selectable
  )

  if (-not [DesktopTodo.Win32]::IsWindow($Hwnd)) {
    return @{ ok = $false; error = '窗口句柄无效' }
  }

  $W = [DesktopTodo.Win32]
  $ex = $W::GetWindowLongPtr($Hwnd, $W::GWL_EXSTYLE).ToInt64()

  $transparent = [int64]$W::WS_EX_TRANSPARENT
  $layered     = [int64]$W::WS_EX_LAYERED
  $noActivate  = [int64]$W::WS_EX_NOACTIVATE

  if ($Selectable) {
    $new = $ex -band (-bnot ($transparent -bor $layered -bor $noActivate))
  } else {
    $new = $ex -bor $transparent -bor $layered -bor $noActivate
  }

  [void]$W::SetWindowLongPtr($Hwnd, $W::GWL_EXSTYLE, [IntPtr]$new)

  $flags = $W::SWP_NOMOVE -bor $W::SWP_NOSIZE -bor $W::SWP_NOZORDER -bor `
           $W::SWP_NOACTIVATE -bor $W::SWP_FRAMECHANGED
  $ok = $W::SetWindowPos($Hwnd, [IntPtr]::Zero, 0, 0, 0, 0, $flags)

  if (-not $ok) {
    return @{ ok = $false; error = "应用扩展样式失败，Win32 错误码 $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
  }
  return @{ ok = $true; selectable = $Selectable }
}

function Get-WindowSelectable {
  param([Parameter(Mandatory)][IntPtr]$Hwnd)
  $W = [DesktopTodo.Win32]
  $ex = $W::GetWindowLongPtr($Hwnd, $W::GWL_EXSTYLE).ToInt64()
  return (($ex -band [int64]$W::WS_EX_TRANSPARENT) -ne 0) -eq $false
}

# ============================================================== 几何操作

function Get-WindowBounds {
  param([Parameter(Mandatory)][IntPtr]$Hwnd)
  $r = New-Object DesktopTodo.RECT
  if (-not [DesktopTodo.Win32]::GetWindowRect($Hwnd, [ref]$r)) { return $null }
  return [pscustomobject]@{
    x = $r.Left
    y = $r.Top
    w = $r.Right - $r.Left
    h = $r.Bottom - $r.Top
  }
}

function Move-WindowBy {
  <#
    .SYNOPSIS  按位移移动窗口（CSS px → 物理 px 由 Dpi 参数换算）
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [Parameter(Mandatory)][double]$Dx,
    [Parameter(Mandatory)][double]$Dy,
    [double]$Dpr = 1
  )

  $b = Get-WindowBounds -Hwnd $Hwnd
  if (-not $b) { return @{ ok = $false; error = '无法读取窗口位置' } }

  $nx = [int][Math]::Round($b.x + $Dx * $Dpr)
  $ny = [int][Math]::Round($b.y + $Dy * $Dpr)

  $flags = [DesktopTodo.Win32]::SWP_NOSIZE -bor [DesktopTodo.Win32]::SWP_NOZORDER -bor `
           [DesktopTodo.Win32]::SWP_NOACTIVATE
  $ok = [DesktopTodo.Win32]::SetWindowPos($Hwnd, [IntPtr]::Zero, $nx, $ny, 0, 0, $flags)
  if (-not $ok) { return @{ ok = $false; error = 'SetWindowPos 移动失败' } }
  return @{ ok = $true; bounds = (Get-WindowBounds -Hwnd $Hwnd) }
}

function Resize-WindowBy {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [Parameter(Mandatory)][double]$Dw,
    [Parameter(Mandatory)][double]$Dh,
    [double]$Dpr = 1,
    [int]$MinW = 260,
    [int]$MinH = 240
  )

  $b = Get-WindowBounds -Hwnd $Hwnd
  if (-not $b) { return @{ ok = $false; error = '无法读取窗口尺寸' } }

  $nw = [Math]::Max($MinW, [int][Math]::Round($b.w + $Dw * $Dpr))
  $nh = [Math]::Max($MinH, [int][Math]::Round($b.h + $Dh * $Dpr))

  $ok = [DesktopTodo.Win32]::MoveWindow($Hwnd, [int]$b.x, [int]$b.y, $nw, $nh, $true)
  if (-not $ok) { return @{ ok = $false; error = 'MoveWindow 缩放失败' } }
  return @{ ok = $true; bounds = (Get-WindowBounds -Hwnd $Hwnd) }
}

function Set-WindowBounds {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [AllowNull()][object]$X,
    [AllowNull()][object]$Y,
    [AllowNull()][object]$W,
    [AllowNull()][object]$H,
    [double]$Dpr = 1
  )

  $b = Get-WindowBounds -Hwnd $Hwnd
  if (-not $b) { return @{ ok = $false; error = '无法读取窗口位置' } }

  $nx = if ($null -ne $X) { [int][Math]::Round([double]$X) } else { $b.x }
  $ny = if ($null -ne $Y) { [int][Math]::Round([double]$Y) } else { $b.y }
  $nw = if ($null -ne $W) { [int][Math]::Round([double]$W * $Dpr) } else { $b.w }
  $nh = if ($null -ne $H) { [int][Math]::Round([double]$H * $Dpr) } else { $b.h }

  $ok = [DesktopTodo.Win32]::MoveWindow($Hwnd, $nx, $ny, $nw, $nh, $true)
  if (-not $ok) { return @{ ok = $false; error = 'MoveWindow 失败' } }
  return @{ ok = $true; bounds = (Get-WindowBounds -Hwnd $Hwnd) }
}

function Get-PrimaryWorkArea {
  Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue
  try {
    $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    return [pscustomobject]@{ x = $wa.X; y = $wa.Y; w = $wa.Width; h = $wa.Height }
  } catch {
    return [pscustomobject]@{ x = 0; y = 0; w = 1920; h = 1040 }
  }
}

function Test-BoundsOnScreen {
  <# 判断窗口是否至少有一部分落在可见工作区内，避免重启后窗口跑到屏幕外 #>
  param(
    [Parameter(Mandatory)]$Bounds,
    $WorkArea
  )
  if (-not $WorkArea) { $WorkArea = Get-PrimaryWorkArea }
  $right  = $Bounds.x + $Bounds.w
  $bottom = $Bounds.y + $Bounds.h
  # 至少保留 60px 的可抓取区域
  $visibleX = ($right -gt ($WorkArea.x + 60)) -and ($Bounds.x -lt ($WorkArea.x + $WorkArea.w - 60))
  $visibleY = ($bottom -gt ($WorkArea.y + 10)) -and ($Bounds.y -lt ($WorkArea.y + $WorkArea.h - 30))
  return ($visibleX -and $visibleY)
}

function Show-WindowSafe {
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [switch]$NoActivate
  )
  $cmd = if ($NoActivate) { [DesktopTodo.Win32]::SW_SHOWNOACTIVATE } else { [DesktopTodo.Win32]::SW_RESTORE }
  [void][DesktopTodo.Win32]::ShowWindow($Hwnd, $cmd)
  if (-not $NoActivate) { [void][DesktopTodo.Win32]::SetForegroundWindow($Hwnd) }
}

function Hide-Window {
  param([Parameter(Mandatory)][IntPtr]$Hwnd)
  [void][DesktopTodo.Win32]::ShowWindow($Hwnd, [DesktopTodo.Win32]::SW_HIDE)
}

function Minimize-Window {
  param([Parameter(Mandatory)][IntPtr]$Hwnd)
  [void][DesktopTodo.Win32]::ShowWindow($Hwnd, [DesktopTodo.Win32]::SW_MINIMIZE)
}

function Close-WindowGracefully {
  <# 发 WM_CLOSE 让 Edge 正常退出，避免留下残留进程 #>
  param([Parameter(Mandatory)][IntPtr]$Hwnd)
  [void][DesktopTodo.Win32]::PostMessage($Hwnd, [DesktopTodo.Win32]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
}

# ============================================================== 热键解析

function ConvertTo-HotkeySpec {
  <#
    .SYNOPSIS  把 "Ctrl+Alt+L" 解析为 { modifiers, vk }；解析失败返回 $null
    .NOTES
      不要用 switch 内的 continue 来"跳过本轮循环"——PowerShell 的 continue
      只跳出 switch，随后仍会执行 switch 之后的语句，导致解析被误判为失败。
      这里改用 $recognized 标志显式控制流程。
  #>
  # AllowEmptyString 是必需的：默认 Mandatory 会直接拒绝空串并抛错，
  # 而调用方（如页面传来空的热键配置）期望的是"解析失败返回 $null"。
  param([Parameter(Mandatory)][AllowEmptyString()][string]$Text)

  if ([string]::IsNullOrWhiteSpace($Text)) { return $null }

  $mods = 0
  $vk = 0
  $parts = $Text -split '\+'

  foreach ($raw in $parts) {
    $p = $raw.Trim()
    if (-not $p) { continue }

    $recognized = $false
    switch -Regex ($p) {
      '^(Ctrl|Control)$' { $mods = $mods -bor [DesktopTodo.Win32]::MOD_CONTROL; $recognized = $true }
      '^Alt$'            { $mods = $mods -bor [DesktopTodo.Win32]::MOD_ALT;     $recognized = $true }
      '^Shift$'          { $mods = $mods -bor [DesktopTodo.Win32]::MOD_SHIFT;   $recognized = $true }
      '^(Win|Meta)$'     { $mods = $mods -bor 0x0008;                           $recognized = $true }
    }
    if ($recognized) { continue }

    if ($p.Length -eq 1) {
      $ch = $p.ToUpperInvariant()[0]
      if (($ch -ge 'A' -and $ch -le 'Z') -or ($ch -ge '0' -and $ch -le '9')) {
        $vk = [int][char]$ch
        continue
      }
    }

    if ($p -match '^F([1-9]|1[0-9]|2[0-4])$') {
      $vk = 0x70 + [int]$Matches[1] - 1
      continue
    }

    # 出现无法识别的片段（如拼错 "Ctrk"）时整体判为非法，避免默默绑定错键
    return $null
  }

  if ($vk -eq 0 -or $mods -eq 0) { return $null }
  return [pscustomobject]@{
    modifiers = [uint32]($mods -bor [DesktopTodo.Win32]::MOD_NOREPEAT)
    vk        = [uint32]$vk
  }
}

# ============================================================== 开机自启

$script:RunKeyPath  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$script:RunValueName = 'DesktopTodoList'

function Get-AutoStartCommand {
  <# 自启命令：用 wscript 或直接 powershell 启动 launcher.ps1，并带上 --autostart #>
  param([Parameter(Mandatory)][string]$LauncherPath)
  return 'powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' +
         $LauncherPath + '" -AutoStart'
}

function Get-AutoStartState {
  [CmdletBinding()]
  param([string]$LauncherPath)

  try {
    $val = (Get-ItemProperty -Path $script:RunKeyPath -Name $script:RunValueName -ErrorAction Stop).$script:RunValueName
    if (-not $val) { return @{ enabled = $false; command = $null } }
    # 注册项存在但指向别的路径，视为"已开启但不是本副本"，仍报告 enabled 并附实际命令
    return @{ enabled = $true; command = $val }
  } catch {
    return @{ enabled = $false; command = $null }
  }
}

function Set-AutoStart {
  <#
    .SYNOPSIS  开启/关闭开机自启；失败时返回明确错误而不是静默失败（风险 R7）
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][bool]$Enabled,
    [Parameter(Mandatory)][string]$LauncherPath
  )

  try {
    if ($Enabled) {
      if (-not (Test-Path -LiteralPath $LauncherPath)) {
        return @{ ok = $false; error = "找不到启动脚本：$LauncherPath" }
      }
      $cmd = Get-AutoStartCommand -LauncherPath $LauncherPath
      if (-not (Test-Path $script:RunKeyPath)) {
        New-Item -Path $script:RunKeyPath -Force | Out-Null
      }
      Set-ItemProperty -Path $script:RunKeyPath -Name $script:RunValueName -Value $cmd -ErrorAction Stop

      # 回读确认写入成功
      $check = (Get-ItemProperty -Path $script:RunKeyPath -Name $script:RunValueName -ErrorAction Stop).$script:RunValueName
      if (-not $check) { return @{ ok = $false; error = '注册表写入后回读为空' } }
      return @{ ok = $true; enabled = $true; command = $cmd }
    }
    else {
      if (Get-ItemProperty -Path $script:RunKeyPath -Name $script:RunValueName -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $script:RunKeyPath -Name $script:RunValueName -ErrorAction Stop
      }
      return @{ ok = $true; enabled = $false }
    }
  } catch {
    return @{ ok = $false; error = "注册表操作失败：$($_.Exception.Message)" }
  }
}

# ============================================================== 消息窗口

function New-MessageWindow {
  <#
    .SYNOPSIS  创建不可见的消息窗口，用于接收 WM_HOTKEY 与托盘回调
    .OUTPUTS   句柄与窗口过程委托（委托必须保留引用，否则会被 GC 回收导致崩溃）
  #>
  [CmdletBinding()]
  param()

  $W = [DesktopTodo.Win32]
  $script:wndProcRef = [DesktopTodo.WndProcDelegate] {
    param([IntPtr]$hWnd, [uint32]$msg, [IntPtr]$wParam, [IntPtr]$lParam)

    if ($script:OnWindowMessage) {
      $handled = $false
      try {
        $handled = & $script:OnWindowMessage $hWnd $msg $wParam $lParam
      } catch {
        Write-Host "[host] 消息处理异常: $($_.Exception.Message)" -ForegroundColor DarkYellow
      }
      if ($handled) { return [IntPtr]::Zero }
    }
    return [DesktopTodo.Win32]::DefWindowProc($hWnd, $msg, $wParam, $lParam)
  }

  $hInstance = [DesktopTodo.Win32]::GetModuleHandle($null)
  $className = 'DesktopTodoMessageWindow'

  $wc = New-Object DesktopTodo.WNDCLASSEX
  $wc.cbSize        = [Runtime.InteropServices.Marshal]::SizeOf([type][DesktopTodo.WNDCLASSEX])
  $wc.style         = 0
  $wc.lpfnWndProc   = [Runtime.InteropServices.Marshal]::GetFunctionPointerForDelegate($script:wndProcRef)
  $wc.cbClsExtra    = 0
  $wc.cbWndExtra    = 0
  $wc.hInstance     = $hInstance
  $wc.hIcon         = [IntPtr]::Zero
  $wc.hCursor       = [IntPtr]::Zero
  $wc.hbrBackground = [IntPtr]::Zero
  $wc.lpszMenuName  = $null
  $wc.lpszClassName = $className
  $wc.hIconSm       = [IntPtr]::Zero

  $atom = [DesktopTodo.Win32]::RegisterClassEx([ref]$wc)
  if ($atom -eq 0) {
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    # 1410 = 类已注册（同一进程内重复调用），可继续
    if ($err -ne 1410) { throw "RegisterClassEx 失败，错误码 $err" }
  }

  # 尺寸 0x0 且不显示：仅用于收消息，不会出现在屏幕上
  $hwnd = [DesktopTodo.Win32]::CreateWindowEx(0, $className, 'DesktopTodoHost', 0,
    0, 0, 0, 0, [IntPtr]::Zero, [IntPtr]::Zero, $hInstance, [IntPtr]::Zero)
  if ($hwnd -eq [IntPtr]::Zero) {
    throw "CreateWindowEx 失败，错误码 $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
  }
  return $hwnd
}

function Start-MessageLoop {
  <# 阻塞式消息循环；收到 WM_QUIT 时返回 #>
  $msg = New-Object DesktopTodo.MSG
  while ($true) {
    $r = [DesktopTodo.Win32]::GetMessage([ref]$msg, [IntPtr]::Zero, 0, 0)
    if ($r -eq 0 -or $r -eq -1) { break }   # 0 = WM_QUIT，-1 = 错误
    [void][DesktopTodo.Win32]::TranslateMessage([ref]$msg)
    [void][DesktopTodo.Win32]::DispatchMessage([ref]$msg)
  }
}

function Stop-MessageLoop {
  [void][DesktopTodo.Win32]::PostQuitMessage(0)
}

# ============================================================== 托盘图标

function New-TrayIcon {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [string]$Tooltip = '待办清单',
    [string]$IconSource
  )

  $W = [DesktopTodo.Win32]
  $hicon = [IntPtr]::Zero

  if ($IconSource -and (Test-Path -LiteralPath $IconSource)) {
    try {
      $large = New-Object IntPtr[] 1
      $small = New-Object IntPtr[] 1
      $n = $W::ExtractIconEx($IconSource, 0, $large, $small, 1)
      if ($n -gt 0) { $hicon = $small[0] }
    } catch { $hicon = [IntPtr]::Zero }
  }
  if ($hicon -eq [IntPtr]::Zero) {
    # 回退到系统默认图标（IDI_APPLICATION = 32512）
    $hicon = [DesktopTodo.Win32]::LoadIcon([IntPtr]::Zero, 32512)
  }

  $nid = New-Object DesktopTodo.NOTIFYICONDATA
  $nid.cbSize           = [Runtime.InteropServices.Marshal]::SizeOf([type][DesktopTodo.NOTIFYICONDATA])
  $nid.hWnd             = $Hwnd
  $nid.uID              = 1
  $nid.uFlags           = $W::NIF_MESSAGE -bor $W::NIF_ICON -bor $W::NIF_TIP
  $nid.uCallbackMessage = $W::WM_TRAYICON
  $nid.hIcon            = $hicon
  $nid.szTip            = $Tooltip
  $nid.szInfo           = ''
  $nid.szInfoTitle      = ''
  $nid.guidItem         = [Guid]::Empty
  $nid.hBalloonIcon     = [IntPtr]::Zero

  $ok = $W::Shell_NotifyIcon($W::NIM_ADD, [ref]$nid)
  if (-not $ok) { return @{ ok = $false; error = 'Shell_NotifyIcon(NIM_ADD) 失败，托盘可能不可用' } }

  $nid.uTimeoutOrVersion = $W::NOTIFYICON_VERSION_4
  [void]$W::Shell_NotifyIcon($W::NIM_SETVERSION, [ref]$nid)

  $script:trayNid = $nid
  return @{ ok = $true }
}

function Update-TrayTooltip {
  param([Parameter(Mandatory)][string]$Tooltip)
  if (-not $script:trayNid) { return }
  $W = [DesktopTodo.Win32]
  $nid = $script:trayNid
  $nid.uFlags = $W::NIF_TIP
  $nid.szTip  = $Tooltip
  [void]$W::Shell_NotifyIcon($W::NIM_MODIFY, [ref]$nid)
  $script:trayNid = $nid
}

function Show-TrayBalloon {
  param(
    [Parameter(Mandatory)][string]$Title,
    [Parameter(Mandatory)][string]$Text,
    [uint32]$Flags = 0x00000001   # NIIF_INFO
  )
  if (-not $script:trayNid) { return }
  $W = [DesktopTodo.Win32]
  $nid = $script:trayNid
  $nid.uFlags      = $W::NIF_INFO
  $nid.szInfoTitle = $Title
  $nid.szInfo      = $Text
  $nid.dwInfoFlags = $Flags
  [void]$W::Shell_NotifyIcon($W::NIM_MODIFY, [ref]$nid)
  $script:trayNid = $nid
}

function Remove-TrayIcon {
  if (-not $script:trayNid) { return }
  [void][DesktopTodo.Win32]::Shell_NotifyIcon([DesktopTodo.Win32]::NIM_DELETE, [ref]$script:trayNid)
  $script:trayNid = $null
}

function Show-TrayMenu {
  <#
    .SYNOPSIS  弹出托盘右键菜单并返回被点击项的 ID（未选择返回 0）
    .PARAMETER Items  数组，每项为 @{ id; text; checked; separator }
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][IntPtr]$Hwnd,
    [Parameter(Mandatory)][array]$Items
  )

  $W = [DesktopTodo.Win32]
  $menu = $W::CreatePopupMenu()
  if ($menu -eq [IntPtr]::Zero) { return 0 }

  try {
    # TrackPopupMenu 要求窗口是前台窗口，否则点菜单外部不会关闭菜单
    [void]$W::SetForegroundWindow($Hwnd)

    foreach ($it in $Items) {
      if ($it.separator) {
        [void]$W::AppendMenu($menu, $W::MF_SEPARATOR, [IntPtr]::Zero, $null)
        continue
      }
      $flags = $W::MF_STRING
      if ($it.checked) { $flags = $flags -bor $W::MF_CHECKED }
      [void]$W::AppendMenu($menu, $flags, [IntPtr][int]$it.id, [string]$it.text)
    }

    $pt = New-Object DesktopTodo.POINT
    [void]$W::GetCursorPos([ref]$pt)

    $cmd = $W::TrackPopupMenu($menu,
      $W::TPM_RIGHTBUTTON -bor $W::TPM_RETURNCMD -bor $W::TPM_NONOTIFY,
      $pt.X, $pt.Y, 0, $Hwnd, [IntPtr]::Zero)

    return [int]$cmd
  } finally {
    [void]$W::DestroyMenu($menu)
  }
}

# ============================================================== 进程/窗口查找

function Get-EdgePath {
  <# 依次探测常见 Edge 安装位置；找不到返回 $null（风险 R9） #>
  $candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path $env:ProgramFiles        'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path $env:LOCALAPPDATA        'Microsoft\Edge\Application\msedge.exe')
  )
  foreach ($c in $candidates) {
    if ($c -and (Test-Path -LiteralPath $c)) { return $c }
  }
  # 最后尝试注册表
  try {
    $reg = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\msedge.exe' -ErrorAction Stop
    if ($reg.'(default)' -and (Test-Path -LiteralPath $reg.'(default)')) { return $reg.'(default)' }
  } catch { }
  return $null
}

function Get-ProcessMainWindowHandle {
  <#
    .SYNOPSIS  轮询查找标题匹配的窗口句柄
    .DESCRIPTION
      Edge --app 模式的窗口标题取自页面 <title>，因此按标题匹配即可定位。
      带超时，避免 Edge 启动失败时无限等待。
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][string]$TitleLike,
    [int]$TimeoutSeconds = 25,
    [int]$ExcludePid = 0
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  while ((Get-Date) -lt $deadline) {
    $procs = Get-Process -Name msedge -ErrorAction SilentlyContinue
    foreach ($p in $procs) {
      if ($ExcludePid -and $p.Id -eq $ExcludePid) { continue }
      if ($p.MainWindowHandle -ne [IntPtr]::Zero -and $p.MainWindowTitle -like $TitleLike) {
        return $p.MainWindowHandle
      }
    }
    Start-Sleep -Milliseconds 180
  }
  return [IntPtr]::Zero
}
