[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Exe,
  [Parameter(Mandatory)][string]$Version,
  [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$checks = 0

function Assert-Metadata([string]$Name, [bool]$Condition, [string]$Details = '') {
  $script:checks++
  if (-not $Condition) { throw "[FAIL] $Name $Details" }
  Write-Host "[PASS] $Name"
}

$stream = [IO.File]::OpenRead($exePath)
try {
  $reader = [IO.BinaryReader]::new($stream)
  $stream.Position = 0x3c
  $peOffset = $reader.ReadInt32()
  $stream.Position = $peOffset
  $signature = $reader.ReadUInt32()
  $machine = $reader.ReadUInt16()
} finally {
  $stream.Dispose()
}
$expectedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0xaa64 }
Assert-Metadata 'PE 架构与目标一致' ($signature -eq 0x00004550 -and $machine -eq $expectedMachine) ("machine=0x{0:X4}" -f $machine)

$versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($exePath)
Assert-Metadata '产品名称' ($versionInfo.ProductName -eq 'DesktopTodoList') ("actual='$($versionInfo.ProductName)'")
Assert-Metadata '公司占位名称已填写' (-not [string]::IsNullOrWhiteSpace($versionInfo.CompanyName))
Assert-Metadata '产品版本与请求的 SemVer 一致' ($versionInfo.ProductVersion -eq $Version) ("actual='$($versionInfo.ProductVersion)'")
Assert-Metadata '原始文件名' ($versionInfo.OriginalFilename -eq 'DesktopTodoList.exe') ("actual='$($versionInfo.OriginalFilename)'")

$nativeManifestReader = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeManifestReader {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern uint SizeofResource(IntPtr module, IntPtr resource);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern IntPtr LockResource(IntPtr resourceData);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern bool FreeLibrary(IntPtr module);
  public static string Read(string fileName) {
    IntPtr module = LoadLibraryEx(fileName, IntPtr.Zero, 0x00000002);
    if (module == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
    try {
      IntPtr resource = FindResource(module, new IntPtr(1), new IntPtr(24));
      if (resource == IntPtr.Zero) throw new InvalidOperationException("RT_MANIFEST resource #1 is missing");
      uint size = SizeofResource(module, resource);
      IntPtr data = LockResource(LoadResource(module, resource));
      if (data == IntPtr.Zero || size == 0) throw new InvalidOperationException("Manifest resource is empty");
      byte[] bytes = new byte[size];
      Marshal.Copy(data, bytes, 0, bytes.Length);
      return Encoding.UTF8.GetString(bytes);
    } finally { FreeLibrary(module); }
  }
}
'@
Add-Type -TypeDefinition $nativeManifestReader -ErrorAction SilentlyContinue
$manifest = [NativeManifestReader]::Read($exePath)
Assert-Metadata '应用清单版本与请求版本一致' ($manifest -match ('(?i)assemblyIdentity[^>]+version="' + [regex]::Escape("$Version.0") + '"'))
Assert-Metadata '执行级别不请求管理员权限' ($manifest -match 'requestedExecutionLevel[^>]+level="asInvoker"')
Assert-Metadata '启用 PerMonitorV2 DPI 感知' ($manifest -match '(?is)<dpiAwareness[^>]*>\s*PerMonitorV2\s*</dpiAwareness>')
Assert-Metadata '声明 Windows 8.1 兼容 GUID' ($manifest -match '(?i)1f676c76-80e1-4239-95bb-83d0f6d0da78')
Assert-Metadata '声明 Windows 10/11 兼容 GUID' ($manifest -match '(?i)8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a')

$stdoutFile = New-TemporaryFile
try {
  $process = Start-Process -FilePath $exePath -ArgumentList '--version' -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutFile.FullName
  $output = (Get-Content -LiteralPath $stdoutFile.FullName -Raw).Trim()
  Assert-Metadata '--version 返回成功' ($process.ExitCode -eq 0) ("exit=$($process.ExitCode)")
  Assert-Metadata '--version 与文件资源一致' ($output -eq "DesktopTodoList $Version") ("actual='$output'")
} finally {
  Remove-Item -LiteralPath $stdoutFile.FullName -Force -ErrorAction SilentlyContinue
}

Write-Host "Release metadata checks: $checks passed."
