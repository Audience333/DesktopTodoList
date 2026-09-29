[CmdletBinding()]
param([Parameter(Mandatory)][string]$Path)

$ErrorActionPreference = 'Stop'
$target = (Resolve-Path -LiteralPath $Path).Path
$encodedCertificate = $env:WINDOWS_SIGNING_CERTIFICATE_BASE64
$certificatePassword = $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD
if ([string]::IsNullOrWhiteSpace($encodedCertificate) -or [string]::IsNullOrEmpty($certificatePassword)) {
  throw 'Signing certificate secrets are not configured.'
}

$tool = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $tool) {
  $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
  $candidates = @(Get-ChildItem -LiteralPath $sdkRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
    Where-Object { Test-Path -LiteralPath $_ })
  if ($candidates.Count -eq 0) { throw 'Windows SDK signtool.exe was not found.' }
  $tool = [pscustomobject]@{ Source = $candidates[0] }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-sign-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$certificateFile = Join-Path $scratch 'signing-certificate.pfx'
$securePassword = ConvertTo-SecureString -String $certificatePassword -AsPlainText -Force
$certificate = $null
try {
  $certificateBytes = [Convert]::FromBase64String($encodedCertificate)
  [IO.File]::WriteAllBytes($certificateFile, $certificateBytes)
  $certificate = Import-PfxCertificate -FilePath $certificateFile `
    -CertStoreLocation 'Cert:\CurrentUser\My' -Password $securePassword
  if (-not $certificate -or -not $certificate.HasPrivateKey) { throw 'The configured certificate has no private signing key.' }

  $start = [Diagnostics.ProcessStartInfo]::new()
  $start.FileName = $tool.Source
  $start.UseShellExecute = $false
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  foreach ($argument in @('sign', '/fd', 'SHA256', '/sha1', $certificate.Thumbprint,
      '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', $target)) {
    [void]$start.ArgumentList.Add($argument)
  }
  $process = [Diagnostics.Process]::Start($start)
  $stdout = $process.StandardOutput.ReadToEnd()
  $stderr = $process.StandardError.ReadToEnd()
  $process.WaitForExit()
  if ($process.ExitCode -ne 0) { throw "signtool sign failed with exit code $($process.ExitCode): $stdout $stderr" }

  $signature = Get-AuthenticodeSignature -LiteralPath $target
  if ($signature.Status -ne 'Valid') { throw "Signed artifact verification returned $($signature.Status)." }
  Write-Host "Signature verified: $([IO.Path]::GetFileName($target))"
} finally {
  if ($certificate) {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -Force -ErrorAction SilentlyContinue
  }
  Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
  $securePassword.Dispose()
  $certificatePassword = $null
  $encodedCertificate = $null
}
