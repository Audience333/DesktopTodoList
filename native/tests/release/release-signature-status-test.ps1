$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$policy = Join-Path $repo 'scripts\release-signature-status.ps1'
if (-not (Test-Path -LiteralPath $policy -PathType Leaf)) {
  throw 'Release signature policy is not implemented.'
}

$certificateBefore = $env:WINDOWS_SIGNING_CERTIFICATE_BASE64
$passwordBefore = $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD
try {
  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = ''
  $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = ''
  $status = (& $policy | Out-String).Trim()
  if ($status -cne 'unsigned') { throw "Missing secrets must select unsigned status, got '$status'." }
  Write-Host '[PASS] Missing certificate secrets select an explicitly unsigned release'

  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = 'dGVzdA=='
  $partialRejected = $false
  try { & $policy | Out-Null } catch { $partialRejected = $true }
  if (-not $partialRejected) { throw 'An incomplete signing-secret pair must reject the release.' }
  Write-Host '[PASS] Incomplete certificate secrets reject release configuration'

  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = ''
  $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = 'test-password'
  $passwordOnlyRejected = $false
  try { & $policy | Out-Null } catch { $passwordOnlyRejected = $true }
  if (-not $passwordOnlyRejected) { throw 'A password without a certificate must reject the release.' }
  Write-Host '[PASS] Password-only configuration is rejected'

  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = 'dGVzdA=='
  $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = 'test-password'
  $status = (& $policy | Out-String).Trim()
  if ($status -cne 'valid') { throw "A complete signing-secret pair must select valid status, got '$status'." }
  Write-Host '[PASS] Complete certificate secrets select signed release status'
} finally {
  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = $certificateBefore
  $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = $passwordBefore
}
Write-Host 'Release signature status checks: PASS.'
