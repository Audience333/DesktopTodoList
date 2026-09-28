param(
    [string]$WindowTitle = 'DesktopTodoList'
)

$nativeMethods = @'
using System;
using System.Runtime.InteropServices;
public static class WidgetWindowChecks {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindWindow(string className, string windowName);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll", EntryPoint = "keybd_event", SetLastError = true)]
    public static extern void KeybdEvent(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
}
'@

Add-Type -TypeDefinition $nativeMethods
$window = [WidgetWindowChecks]::FindWindow($null, $WindowTitle)
if ($window -eq [IntPtr]::Zero) {
    Write-Output 'SKIP: start the native widget in the interactive desktop session first.'
    exit 2
}

$styleIndex = -20
$transparent = [Int64]0x20
$originalStyle = [WidgetWindowChecks]::GetWindowLongPtr($window, $styleIndex).ToInt64()
$originalState = (($originalStyle -band $transparent) -ne 0)

function Get-ClickThroughState {
    $style = [WidgetWindowChecks]::GetWindowLongPtr($window, $styleIndex).ToInt64()
    return (($style -band $transparent) -ne 0)
}

function Send-RecoveryChord {
    $keys = [byte[]](0x11, 0x12, 0x4C)
    foreach ($key in $keys) { [WidgetWindowChecks]::KeybdEvent($key, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 80
    for ($index = $keys.Length - 1; $index -ge 0; $index--) {
        [WidgetWindowChecks]::KeybdEvent($keys[$index], 0, 2, [UIntPtr]::Zero)
    }
}

try {
    Send-RecoveryChord
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ((Get-ClickThroughState) -eq $originalState -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if ((Get-ClickThroughState) -eq $originalState) {
        Write-Output 'FAIL: Ctrl+Alt+L did not change the click-through style.'
        exit 1
    }

    Send-RecoveryChord
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ((Get-ClickThroughState) -ne $originalState -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if ((Get-ClickThroughState) -ne $originalState) {
        Write-Output 'FAIL: the recovery chord did not restore the original interaction state.'
        exit 1
    }
    Write-Output 'PASS: Ctrl+Alt+L toggled and restored the native widget interaction style.'
} finally {
    if ((Get-ClickThroughState) -ne $originalState) { Send-RecoveryChord }
}
