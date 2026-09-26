# =============================================================================
# tests/test-server.ps1 — 独立验证静态服务器与宿主指令通道
# 只测试 server.ps1，不启动 Edge、不注册热键，因此可在任何环境安全运行。
# 运行：powershell -ExecutionPolicy Bypass -File tests\test-server.ps1
# =============================================================================
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DdxRoot     = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$DdxSiteRoot = Join-Path $DdxRoot 'src'

$passed = 0
$failed = @()

function Check {
  param([string]$Label, [bool]$Cond, [string]$Detail = '')
  if ($Cond) {
    $script:passed++
    Write-Host "  [OK]   $Label" -ForegroundColor Green
  } else {
    $script:failed += ($Label + $(if ($Detail) { "  [$Detail]" } else { '' }))
    Write-Host "  [FAIL] $Label  $Detail" -ForegroundColor Red
  }
}

Write-Host ''
Write-Host '启动服务器模块…' -ForegroundColor Cyan
. (Join-Path $DdxRoot 'host\server.ps1')

# ------------------------------------------------------------------ 启动
$srv = Start-StaticServer -Root $DdxSiteRoot
if (-not $srv.ok) {
  Write-Host "服务器启动失败：$($srv.error)" -ForegroundColor Red
  exit 1
}
Write-Host "已监听 $($srv.url)" -ForegroundColor DarkGray
[void](Start-ServerAsyncWait)

<#
  关键点：请求只有在 Invoke-ServerStep 被调用时才会被处理。
  因此不能直接用同步的 Invoke-WebRequest —— 它会一直等响应，而服务器
  主线程还没开始泵请求，形成死锁。

  解法：把请求放到后台 Job 里发，主线程负责泵服务器。
#>
function Request-WithPump {
  param(
    [string]$Method,
    [string]$Url,
    [string]$Json,
    [int]$PumpIterations = 600
  )

  $job = Start-Job -ScriptBlock {
    param($m, $u, $j)
    try {
      if ($m -eq 'GET') {
        $r = Invoke-WebRequest -Uri $u -UseBasicParsing -TimeoutSec 8
      } else {
        $r = Invoke-WebRequest -Uri $u -Method POST -Body $j -ContentType 'application/json' `
             -UseBasicParsing -TimeoutSec 8
      }
      return @{ status = $r.StatusCode; body = [string]$r.Content; ctype = [string]$r.Headers['Content-Type'] }
    } catch {
      $resp = $_.Exception.Response
      if ($resp) {
        $sr = New-Object System.IO.StreamReader($resp.GetResponseStream())
        return @{ status = [int]$resp.StatusCode; body = $sr.ReadToEnd(); ctype = '' }
      }
      return @{ status = -1; body = $_.Exception.Message; ctype = '' }
    }
  } -ArgumentList $Method, $Url, $Json

  $deadline = (Get-Date).AddSeconds(15)
  while ($job.State -eq 'Running' -and (Get-Date) -lt $deadline) {
    [void](Invoke-ServerStep)
    Start-Sleep -Milliseconds 12
  }
  # 收尾再泵几轮，确保响应体写完
  for ($i = 0; $i -lt $PumpIterations; $i++) { [void](Invoke-ServerStep) }

  $out = $null
  if ($job.State -eq 'Completed') { $out = Receive-Job $job }
  Remove-Job $job -Force

  if (-not $out) { return @{ status = -1; body = 'job timeout or failure'; ctype = '' } }
  return $out
}

$base = 'http://127.0.0.1:' + $srv.port
$tok  = $srv.token

# ------------------------------------------------------------------ 测试
Write-Host ''
Write-Host '静态文件服务' -ForegroundColor Cyan

$r = Request-WithPump -Method GET -Url "$base/index.html"
Check 'GET /index.html 返回 200' ($r.status -eq 200) "status=$($r.status)"
Check 'index.html 内容正确' ($r.body -match '<title>待办清单</title>') "len=$($r.body.Length)"
Check 'HTML Content-Type 正确' ($r.ctype -match 'text/html') "ctype=$($r.ctype)"

$r = Request-WithPump -Method GET -Url "$base/js/store.js"
Check 'GET /js/store.js 返回 200' ($r.status -eq 200) "status=$($r.status)"
Check 'JS Content-Type 正确' ($r.ctype -match 'javascript') "ctype=$($r.ctype)"
Check 'JS 内容正确' ($r.body -match 'App\.Store') ''

$r = Request-WithPump -Method GET -Url "$base/styles.css"
Check 'GET /styles.css 返回 200' ($r.status -eq 200) "status=$($r.status)"
Check 'CSS Content-Type 正确' ($r.ctype -match 'text/css') "ctype=$($r.ctype)"

$r = Request-WithPump -Method GET -Url "$base/"
Check 'GET / 回退到 index.html' ($r.status -eq 200 -and $r.body -match '<title>') "status=$($r.status)"

Write-Host ''
Write-Host '安全：目录穿越与 token 校验' -ForegroundColor Cyan

$r = Request-WithPump -Method GET -Url "$base/../host/launcher.ps1"
Check '路径穿越不泄露 host 目录' (-not ($r.body -match 'launcher')) "status=$($r.status)"

$r = Request-WithPump -Method GET -Url "$base/../../Windows/win.ini"
Check '多级穿越被拒绝' (-not ($r.body -match '\[fonts\]')) "status=$($r.status)"

$r = Request-WithPump -Method GET -Url "$base/no-such-file.js"
Check '不存在的文件返回 404' ($r.status -eq 404) "status=$($r.status)"

$r = Request-WithPump -Method GET -Url "$base/__host/resp"
Check '缺少 token 的 API 请求被拒（403）' ($r.status -eq 403) "status=$($r.status)"

$r = Request-WithPump -Method GET -Url "$base/__host/resp?t=wrong-token"
Check '错误 token 被拒（403）' ($r.status -eq 403) "status=$($r.status)"

Write-Host ''
Write-Host '宿主指令通道' -ForegroundColor Cyan

$r = Request-WithPump -Method GET -Url "$base/__host/ping?t=$tok"
Check 'ping 正常响应' ($r.status -eq 200 -and $r.body -match '"ok":true') "status=$($r.status) body=$($r.body)"

$r = Request-WithPump -Method GET -Url "$base/__host/resp?t=$tok"
Check 'resp 返回合法 JSON' ($r.status -eq 200 -and $r.body -match '"caps"') "status=$($r.status) body=$($r.body)"

$payload = '[{"cmd":"set-layer","payload":{"layer":"top"},"at":1},{"cmd":"counts","payload":{"pending":3,"overdue":1},"at":2},{"cmd":"minimize","payload":null,"at":3}]'
$r = Request-WithPump -Method POST -Url "$base/__host/cmd?t=$tok" -Json $payload
Check 'POST 指令被接受' ($r.status -eq 200 -and $r.body -match '"accepted":3') "status=$($r.status) body=$($r.body)"

$queued = @(Receive-HostCommand)
Check '宿主取出 3 条指令' ($queued.Count -eq 3) "count=$($queued.Count)"
Check '指令顺序与内容正确' ($queued[0].cmd -eq 'set-layer' -and $queued[2].cmd -eq 'minimize') `
      ("cmds=" + (($queued | ForEach-Object { $_.cmd }) -join ','))

$again = @(Receive-HostCommand)
Check '指令被消费后队列为空' ($again.Count -eq 0) "count=$($again.Count)"

Set-HostCaps @{ topmost = $true; bottom = $true; clickThrough = $true; autostart = $true; tray = $false }
Set-HostState @{ layer = 'bottom'; selectable = $false }
Publish-HostResponse
$r = Request-WithPump -Method GET -Url "$base/__host/resp?t=$tok"
Check 'resp 含能力信息' ($r.body -match '"clickThrough":true') "body=$($r.body)"
Check 'resp 含实际状态' ($r.body -match '"layer":"bottom"' -and $r.body -match '"selectable":false') "body=$($r.body)"

Add-HostError '模拟的错误'
Publish-HostResponse
$r = Request-WithPump -Method GET -Url "$base/__host/resp?t=$tok"
Check '错误被回传给页面' ($r.body -match '模拟的错误') "body=$($r.body)"

$many = (1..80 | ForEach-Object { '{"cmd":"counts","payload":{"pending":' + $_ + '},"at":' + $_ + '}' }) -join ','
$r = Request-WithPump -Method POST -Url "$base/__host/cmd?t=$tok" -Json "[$many]"
$q = @(Receive-HostCommand)
Check '大批量指令全部入队' ($q.Count -eq 80) "count=$($q.Count)"

$r = Request-WithPump -Method POST -Url "$base/__host/cmd?t=$tok" -Json '{ this is not json'
Check '畸形指令 JSON 被容错处理' ($r.status -eq 200) "status=$($r.status)"

$r = Request-WithPump -Method GET -Url "$base/__host/unknown?t=$tok"
Check '未知 API 子路径返回 404' ($r.status -eq 404) "status=$($r.status)"

$r = Request-WithPump -Method GET -Url "$base/index.html"
Check '异常处理后服务器仍可服务' ($r.status -eq 200) "status=$($r.status)"

# ------------------------------------------------------------------ 收尾
Stop-StaticServer
Write-Host ''
Write-Host ('-' * 58)
if ($failed.Count -eq 0) {
  Write-Host "全部通过  $passed 项断言" -ForegroundColor Green
  exit 0
} else {
  Write-Host "失败 $($failed.Count) 项（通过 $passed 项）" -ForegroundColor Red
  $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
  exit 1
}
