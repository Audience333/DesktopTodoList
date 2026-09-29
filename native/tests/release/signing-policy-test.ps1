$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$signer = Join-Path $repo 'scripts\sign-artifact.ps1'
$testRootName = 'DesktopTodoList-signing-policy-' + [guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) $testRootName
$encodedBefore = $env:WINDOWS_SIGNING_CERTIFICATE_BASE64
$passwordBefore = $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD
$testPassed = $false
New-Item -ItemType Directory -Path $testRoot | Out-Null
$payload = Join-Path $testRoot 'test-payload.exe'
[IO.File]::WriteAllBytes($payload, [byte[]]@(0x4D, 0x5A, 0x00, 0x00))

try {
  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = ''
  $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = ''
  $rejectedMissing = $false
  try { & $signer -Path $payload | Out-Null } catch { $rejectedMissing = $true }
  if (-not $rejectedMissing) { throw 'Signer accepted missing certificate secrets.' }
  Write-Host '[PASS] Signing fails closed when certificate secrets are absent'

  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = 'dGVzdA=='
  $rejectedPartial = $false
  try { & $signer -Path $payload | Out-Null } catch { $rejectedPartial = $true }
  if (-not $rejectedPartial) { throw 'Signer accepted an incomplete secret pair.' }
  Write-Host '[PASS] Signing fails closed when only one certificate secret is configured'
  $testPassed = $true
} finally {
  $env:WINDOWS_SIGNING_CERTIFICATE_BASE64 = $encodedBefore
  $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD = $passwordBefore
  $safeRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
  $safeRelativePath = [IO.Path]::GetRelativePath($safeRoot, [IO.Path]::GetFullPath($testRoot))
  if ($safeRelativePath -eq $testRootName -and
      $testRootName -match '^DesktopTodoList-signing-policy-[0-9a-f]{32}$') {
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}
if (-not $testPassed) { throw 'Signing-policy verification did not complete.' }
Write-Host 'Signing policy checks: PASS.'
