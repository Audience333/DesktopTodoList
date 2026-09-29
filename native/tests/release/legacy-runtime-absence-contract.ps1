[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$check = Join-Path $PSScriptRoot 'legacy-runtime-absence.ps1'
$work = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-legacy-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

function Invoke-LegacyCheck([string]$Root, [string]$ArtifactsDirectory) {
  $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $check, '-Root', $Root)
  if ($ArtifactsDirectory) { $arguments += @('-ArtifactsDirectory', $ArtifactsDirectory) }
  $output = @(& (Join-Path $PSHOME 'pwsh.exe') @arguments 2>&1 | ForEach-Object { "$_" })
  [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output -join "`n" }
}

try {
  $fixtureRoot = Join-Path $work 'source'
  $fakeSource = Join-Path $fixtureRoot 'renamed/runtime/widget.html'
  New-Item -ItemType Directory -Path (Split-Path -Parent $fakeSource) -Force | Out-Null
  [IO.File]::WriteAllText($fakeSource, '<html></html>')
  $sourceResult = Invoke-LegacyCheck -Root $fixtureRoot -ArtifactsDirectory ''
  if ($sourceResult.ExitCode -eq 0 -or $sourceResult.Output -notmatch 'web document/script') {
    throw 'The legacy check did not reject a renamed HTML source file.'
  }
  Write-Output 'PASS: rejects HTML source under an unrecognized path.'

  Remove-Item -LiteralPath $fakeSource
  $artifactRoot = Join-Path $work 'artifacts'
  New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $artifactRoot 'DesktopTodoList-x64.exe'), [Text.Encoding]::ASCII.GetBytes('MZ msedge.exe'))
  $artifactResult = Invoke-LegacyCheck -Root $fixtureRoot -ArtifactsDirectory $artifactRoot
  if ($artifactResult.ExitCode -eq 0 -or $artifactResult.Output -notmatch 'Legacy runtime/launcher marker') {
    throw 'The legacy check did not reject an embedded browser-launch marker.'
  }
  Write-Output 'PASS: rejects a legacy runtime marker embedded in a public executable.'

  Remove-Item -LiteralPath (Join-Path $artifactRoot 'DesktopTodoList-x64.exe')
  $cleanResult = Invoke-LegacyCheck -Root $fixtureRoot -ArtifactsDirectory $artifactRoot
  if ($cleanResult.ExitCode -ne 0) { throw "The legacy check rejected a clean fixture: $($cleanResult.Output)" }
  Write-Output 'PASS: accepts a clean source tree and artifact directory.'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output 'Legacy runtime absence contract: PASS.'
