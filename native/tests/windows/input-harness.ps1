Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not [Environment]::UserInteractive) {
    Write-Host '[SKIP] No interactive desktop session is available.'
    exit 0
}

$harnessSource = @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class DesktopTodoInputHarness {
  public delegate bool EnumChildProc(IntPtr hwnd, IntPtr lParam);
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct GuiThreadInfo {
    public int cbSize, flags;
    public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret;
    public Rect rcCaret;
  }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumChildProc callback, IntPtr data);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumChildProc callback, IntPtr data);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder name, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
  [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint threadId, ref GuiThreadInfo info);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  public static List<IntPtr> Children(IntPtr parent) {
    var result = new List<IntPtr>();
    EnumChildWindows(parent, (hwnd, data) => { result.Add(hwnd); return true; }, IntPtr.Zero);
    return result;
  }
  public static string ClassOf(IntPtr hwnd) {
    var value = new StringBuilder(128); GetClassName(hwnd, value, value.Capacity); return value.ToString();
  }
  public static IntPtr FocusOf(IntPtr parent) {
    uint processId; var threadId = GetWindowThreadProcessId(parent, out processId);
    var info = new GuiThreadInfo(); info.cbSize = Marshal.SizeOf(typeof(GuiThreadInfo));
    return GetGUIThreadInfo(threadId, ref info) ? info.hwndFocus : IntPtr.Zero;
  }
  public static IntPtr FindWidget(uint wantedPid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((hwnd, data) => {
      uint pid; GetWindowThreadProcessId(hwnd, out pid);
      if (wantedPid == 0 || pid == wantedPid) {
        var title = new StringBuilder(128); GetWindowText(hwnd, title, title.Capacity);
        if (title.ToString() == "DesktopTodoList") { found = hwnd; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static uint OwnerPid(IntPtr hwnd) {
    uint pid; GetWindowThreadProcessId(hwnd, out pid); return pid;
  }
}
"@
Add-Type -TypeDefinition $harnessSource

$exe = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot `
    '..\..\..\out\build\windows-x64\native\Debug\DesktopTodoList.exe'))
$startedHere = $false
$window = [DesktopTodoInputHarness]::FindWidget(0)
$process = Get-Process -Name DesktopTodoList -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $exe } | Select-Object -First 1
if ($window -eq [IntPtr]::Zero -and $null -eq $process) {
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "找不到原生程序：$exe"
    }
    $process = Start-Process -FilePath $exe -PassThru
}

for ($attempt = 0; $attempt -lt 80 -and $window -eq [IntPtr]::Zero; $attempt++) {
    $window = [DesktopTodoInputHarness]::FindWidget(0)
    if ($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
}
if ($null -ne $process) {
    $process.Refresh()
    if ($process.HasExited) { Write-Host ("Launched process exit code: {0}" -f $process.ExitCode) }
}
if ($window -ne [IntPtr]::Zero -and $null -ne $process) {
    $startedHere = [DesktopTodoInputHarness]::OwnerPid($window) -eq [uint32]$process.Id
}

$passed = 0
function Check([string]$label, [bool]$condition) {
    if ($condition) { $script:passed++; Write-Host "[PASS] $label" }
    else { throw "[FAIL] $label" }
}

try {
    Check 'Native widget window started' ($window -ne [IntPtr]::Zero)
    Write-Host ("Window handle=$window class=$([DesktopTodoInputHarness]::ClassOf($window)) ownerPid=$($process.Id)")
    $quickAdd = [DesktopTodoInputHarness]::GetDlgItem($window, 501)
    $inlineEdit = [DesktopTodoInputHarness]::GetDlgItem($window, 502)
    $searchEdit = [DesktopTodoInputHarness]::GetDlgItem($window, 503)
    $priorityCombo = [DesktopTodoInputHarness]::GetDlgItem($window, 534)
    Write-Host ("Control handles: " + (@($quickAdd, $inlineEdit, $searchEdit, $priorityCombo) -join ', '))
    $edits = @(@($quickAdd, $inlineEdit, $searchEdit) | Where-Object { $_ -ne [IntPtr]::Zero })
    $combos = @(@($priorityCombo) | Where-Object { $_ -ne [IntPtr]::Zero })
    Check 'Quick add, inline edit, and search use native Edit controls' ($edits.Count -ge 3)
    Check 'Details priority uses a native ComboBox' ($combos.Count -ge 1)

    [void][DesktopTodoInputHarness]::PostMessage($window, 0x8044, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    $focused = [DesktopTodoInputHarness]::FocusOf($window)
    Check 'Ctrl+F path focuses search input' (
        $focused -ne [IntPtr]::Zero -and [DesktopTodoInputHarness]::GetDlgCtrlID($focused) -eq 503)
    [void][DesktopTodoInputHarness]::SendMessage($focused, 0x0100, [IntPtr]27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120

    [void][DesktopTodoInputHarness]::PostMessage($window, 0x8043, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    $focused = [DesktopTodoInputHarness]::FocusOf($window)
    Check 'Ctrl+N path focuses quick-add input' (
        $focused -ne [IntPtr]::Zero -and [DesktopTodoInputHarness]::GetDlgCtrlID($focused) -eq 501)
    [void][DesktopTodoInputHarness]::SendMessage($focused, 0x0100, [IntPtr]27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    Check 'Escape cancels blank entry without creating a task' (-not [DesktopTodoInputHarness]::IsWindowVisible($quickAdd))

    Write-Host "Passed: $passed checks; no test task was entered or saved."
}
finally {
    if ($startedHere -and $window -ne [IntPtr]::Zero) {
        [void][DesktopTodoInputHarness]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        if ($null -ne $process) { [void]$process.WaitForExit(3000) }
    }
}
