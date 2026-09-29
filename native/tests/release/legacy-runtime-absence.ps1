[CmdletBinding()]
param(
  [string]$Root = (Join-Path $PSScriptRoot '..\..\..'),
  [string]$ArtifactsDirectory
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Root).Path
$retiredPaths = @(
  'src',
  'host',
  '启动待办.bat',
  'tests/运行全部测试.bat',
  'tests/run-tests.js',
  'tests/test-backups.js',
  'tests/test-import-export.js',
  'tests/test-persistence.js',
  'tests/test-reminders.js',
  'tests/test-server.ps1',
  'tests/test-window.ps1'
)
$present = @($retiredPaths | Where-Object { Test-Path -LiteralPath (Join-Path $root $_) })
if ($present.Count -gt 0) {
  throw ("Legacy browser runtime or superseded tests are still active: " + ($present -join ', '))
}

$webFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File -Include '*.html', '*.htm', '*.js', '*.mjs', '*.cjs' -ErrorAction Stop | Where-Object {
  $relative = [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
  $relative -notmatch '^(?:\.git|out)/'
})
if ($webFiles.Count -gt 0) {
  throw ("Active tree contains a web document/script file: " + [IO.Path]::GetRelativePath($root, $webFiles[0].FullName))
}

if ($ArtifactsDirectory) {
  $artifactRoot = (Resolve-Path -LiteralPath $ArtifactsDirectory).Path
  $temporary = Join-Path ([IO.Path]::GetTempPath()) ('DesktopTodoList-legacy-scan-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $temporary | Out-Null
  try {
    $targets = @(Get-ChildItem -LiteralPath $artifactRoot -File -Force | Where-Object {
      $_.Extension -in @('.exe', '.zip')
    })
    foreach ($target in $targets) {
      $scanRoot = $null
      if ($target.Extension -eq '.zip') {
        $scanRoot = Join-Path $temporary ([guid]::NewGuid().ToString('N'))
        Expand-Archive -LiteralPath $target.FullName -DestinationPath $scanRoot
        $members = @(Get-ChildItem -LiteralPath $scanRoot -Recurse -File)
        $forbiddenExtensions = @($members | Where-Object { $_.Extension -in @('.html', '.htm', '.js', '.mjs', '.cjs', '.ps1', '.bat', '.cmd', '.vbs', '.hta', '.pdb') })
        if ($forbiddenExtensions.Count -gt 0) {
          throw ("Legacy runtime file in public artifact $($target.Name): $($forbiddenExtensions[0].Name)")
        }
      } else {
        $members = @($target)
      }

      foreach ($member in $members) {
        $isBinary = $member.Extension -eq '.exe'
        if (-not $isBinary -and $member.Extension -notin @('.md', '.txt', '.json', '.ini')) { continue }
        $content = if ($isBinary) {
          [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($member.FullName))
        } else {
          [IO.File]::ReadAllText($member.FullName)
        }
        if ($content -match '(?i)msedge(?:\.exe)?|WebView2?|TcpListener|http://127\.0\.0\.1|localhost\s*(?:server|service)|launcher\.ps1|启动待办\.bat|powershell\.exe') {
          throw ("Legacy runtime/launcher marker in public artifact $($target.Name): $($member.Name)")
        }
      }
    }
  } finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force -ErrorAction SilentlyContinue
  }
}

Write-Output 'PASS: active tree and supplied public artifacts contain no retired web runtime, script launcher, or legacy runtime markers.'
