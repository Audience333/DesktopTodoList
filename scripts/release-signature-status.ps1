[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$encodedCertificate = $env:WINDOWS_SIGNING_CERTIFICATE_BASE64
$certificatePassword = $env:WINDOWS_SIGNING_CERTIFICATE_PASSWORD
$hasCertificate = -not [string]::IsNullOrWhiteSpace($encodedCertificate)
$hasPassword = -not [string]::IsNullOrWhiteSpace($certificatePassword)

if ($hasCertificate -ne $hasPassword) {
  throw 'Both signing secrets must be configured together; refusing an incomplete signing setup.'
}

if ($hasCertificate) {
  Write-Output 'valid'
} else {
  Write-Output 'unsigned'
}
