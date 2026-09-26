# =============================================================================
# server.ps1 — 本地静态服务器 + 宿主指令通道
# -----------------------------------------------------------------------------
# 为什么需要它（见需求文档 §11.3）：
#   1. 给 Edge 一个稳定的 http://127.0.0.1:<port> 源，localStorage 在该源下
#      持久化行为可预期，比 file:// 可靠
#   2. 让宿主能读到页面下发的窗口指令。若页面直接用 file://，宿主就得去解析
#      Chromium 的 LevelDB，代价极高且脆弱
#   3. 作为 keepalive：监听 socket 跟随本进程存活
#
# 为什么用 TcpListener 手写极简 HTTP，而不用 HttpListener：
#   HttpListener 依赖 Windows 内核 http.sys，在部分环境（容器、受限账户、
#   精简系统）构造时即抛 "Operation is not supported on this platform"。
#   TcpListener 只依赖基础 socket，任何环境都可用，而我们只需要
#   "GET 静态文件 + POST 一小段 JSON" 这一点点能力。
#
# 安全：随机 token + 校验 Origin 头；只绑定 127.0.0.1，不对外网暴露。
# =============================================================================

Set-StrictMode -Version Latest

$script:DtsListener   = $null      # TcpListener
$script:DtsPort       = 0
$script:DtsSiteRoot   = $null
$script:DtsToken      = $null
$script:DtsQueue      = New-Object System.Collections.ArrayList
$script:DtsResp       = @{
  at = 0; caps = @{}; bounds = $null; hwnd = $null
  recentErrors = @(); state = @{}
}
$script:DtsPageSeenAt = 0
$script:DtsPending    = $null      # IAsyncResult（异步 accept）
$script:DtsLog        = $null      # 可选日志回调：function($msg)

$script:DtsMimeMap = @{
  '.html'  = 'text/html; charset=utf-8'
  '.htm'   = 'text/html; charset=utf-8'
  '.css'   = 'text/css; charset=utf-8'
  '.js'    = 'application/javascript; charset=utf-8'
  '.mjs'   = 'application/javascript; charset=utf-8'
  '.json'  = 'application/json; charset=utf-8'
  '.svg'   = 'image/svg+xml'
  '.png'   = 'image/png'
  '.jpg'   = 'image/jpeg'
  '.jpeg'  = 'image/jpeg'
  '.gif'   = 'image/gif'
  '.ico'   = 'image/x-icon'
  '.woff'  = 'font/woff'
  '.woff2' = 'font/woff2'
  '.txt'   = 'text/plain; charset=utf-8'
  '.map'   = 'application/json; charset=utf-8'
}

$script:DtsStatusText = @{
  200 = 'OK'; 400 = 'Bad Request'; 403 = 'Forbidden'
  404 = 'Not Found'; 405 = 'Method Not Allowed'; 500 = 'Internal Server Error'
}

function Write-DtsLog {
  param([string]$Message)
  if ($script:DtsLog) { try { & $script:DtsLog $Message } catch { } }
}

# ------------------------------------------------------------------ 工具

function New-HostToken {
  $bytes = New-Object byte[] 16
  $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
  try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
  return -join ($bytes | ForEach-Object { $_.ToString('x2') })
}

function Find-FreePort {
  param([int]$Start = 47821, [int]$Tries = 60)
  for ($i = 0; $i -lt $Tries; $i++) {
    $port = $Start + $i
    $probe = $null
    try {
      $probe = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $port)
      $probe.Start()
      $probe.Stop()
      return $port
    } catch {
      if ($probe) { try { $probe.Stop() } catch { } }
    }
  }
  throw '找不到可用端口（47821-47880 全部被占用）'
}

# ------------------------------------------------------------------ 生命周期

function Start-StaticServer {
  <#
    .SYNOPSIS  在 127.0.0.1 上启动静态服务器
    .OUTPUTS   @{ ok; port; url; token } 或 @{ ok=$false; error }
  #>
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][string]$Root,
    [int]$Port = 0
  )

  if (-not (Test-Path -LiteralPath $Root)) {
    return @{ ok = $false; error = "站点根目录不存在：$Root" }
  }
  $script:DtsSiteRoot = (Resolve-Path -LiteralPath $Root).ProviderPath

  if ($Port -le 0) {
    try { $Port = Find-FreePort } catch { return @{ ok = $false; error = $_.Exception.Message } }
  }

  $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Port)
  try {
    $listener.Start(64)
  } catch {
    return @{ ok = $false; error = "无法监听端口 $Port：$($_.Exception.Message)" }
  }

  $script:DtsListener = $listener
  $script:DtsPort     = $Port
  $script:DtsToken    = New-HostToken

  return @{
    ok    = $true
    port  = $Port
    url   = "http://127.0.0.1:$Port/index.html"
    token = $script:DtsToken
  }
}

function Stop-StaticServer {
  if ($script:DtsListener) {
    try { $script:DtsListener.Stop() } catch { }
    $script:DtsListener = $null
  }
  $script:DtsPending = $null
}

function Start-ServerAsyncWait {
  <#
    .SYNOPSIS  发起一次异步 accept
    .NOTES     绝不能用阻塞的 AcceptTcpClient()：那会让整个消息循环停摆。
  #>
  if (-not $script:DtsListener) { return $false }
  if ($script:DtsPending) { return $true }
  try {
    $script:DtsPending = $script:DtsListener.BeginAcceptTcpClient($null, $null)
    return $true
  } catch {
    Write-DtsLog "接受连接失败：$($_.Exception.Message)"
    return $false
  }
}

# ------------------------------------------------------------------ 响应组装

function Format-HttpResponse {
  param(
    [int]$StatusCode = 200,
    [string]$ContentType = 'text/plain; charset=utf-8',
    [byte[]]$Body
  )

  if ($null -eq $Body) { $Body = New-Object byte[] 0 }
  $statusText = if ($script:DtsStatusText.ContainsKey($StatusCode)) {
    $script:DtsStatusText[$StatusCode]
  } else { 'OK' }

  $head = New-Object System.Text.StringBuilder
  [void]$head.Append("HTTP/1.1 $StatusCode $statusText`r`n")
  [void]$head.Append("Content-Type: $ContentType`r`n")
  [void]$head.Append("Content-Length: $($Body.Length)`r`n")
  [void]$head.Append("Cache-Control: no-store, no-cache, must-revalidate`r`n")
  [void]$head.Append("X-Content-Type-Options: nosniff`r`n")
  # 页面与本服务同源，但仍允许跨源读回执，方便将来换成独立前端调试
  [void]$head.Append("Access-Control-Allow-Origin: *`r`n")
  [void]$head.Append("Access-Control-Allow-Headers: content-type`r`n")
  [void]$head.Append("Connection: close`r`n")
  [void]$head.Append("`r`n")

  $headBytes = [System.Text.Encoding]::ASCII.GetBytes($head.ToString())
  $out = New-Object byte[] ($headBytes.Length + $Body.Length)
  [Array]::Copy($headBytes, 0, $out, 0, $headBytes.Length)
  [Array]::Copy($Body, 0, $out, $headBytes.Length, $Body.Length)
  return $out
}

function Send-Http {
  param(
    [Parameter(Mandatory)]$Stream,
    [int]$StatusCode = 200,
    [string]$ContentType = 'text/plain; charset=utf-8',
    [byte[]]$Body,
    [string]$Text
  )
  if ($null -ne $Text -and $null -eq $Body) {
    $Body = [System.Text.Encoding]::UTF8.GetBytes($Text)
  }
  $bytes = Format-HttpResponse -StatusCode $StatusCode -ContentType $ContentType -Body $Body
  try {
    $Stream.Write($bytes, 0, $bytes.Length)
    $Stream.Flush()
  } catch {
    # 客户端提前断开（页面关闭）属正常情况
  }
}

# ------------------------------------------------------------------ 路径解析

function Resolve-SitePath {
  <#
    .SYNOPSIS  把请求路径安全映射到站点根目录下的真实文件
    .NOTES     必须防目录穿越：解析为绝对路径后再校验前缀
  #>
  param([Parameter(Mandatory)][string]$UrlPath)

  $rel = [System.Uri]::UnescapeDataString($UrlPath)
  $q = $rel.IndexOf('?')
  if ($q -ge 0) { $rel = $rel.Substring(0, $q) }
  $rel = $rel.TrimStart('/')
  if ([string]::IsNullOrWhiteSpace($rel)) { $rel = 'index.html' }

  $parts = New-Object System.Collections.ArrayList
  foreach ($seg in ($rel -split '[\\/]')) {
    if ($seg -eq '' -or $seg -eq '.') { continue }
    if ($seg -eq '..') {
      if ($parts.Count -eq 0) { return $null }   # 试图向上越界
      $parts.RemoveAt($parts.Count - 1)
      continue
    }
    # 拒绝 Windows 保留字符与数据流语法（如 file.txt:stream）
    if ($seg.IndexOfAny([char[]]@(':', '*', '?', '"', '<', '>', '|')) -ge 0) { return $null }
    [void]$parts.Add($seg)
  }
  if ($parts.Count -eq 0) { return $null }

  $full = Join-Path $script:DtsSiteRoot ($parts -join '\')
  try { $full = [System.IO.Path]::GetFullPath($full) } catch { return $null }

  # 双保险：确认最终路径仍在根目录内
  if (-not $full.StartsWith($script:DtsSiteRoot, [StringComparison]::OrdinalIgnoreCase)) {
    return $null
  }
  return $full
}

# ------------------------------------------------------------------ 请求解析

function Read-HttpRequest {
  <#
    .SYNOPSIS  从流里读出一个 HTTP 请求（方法、路径、头、体）
    .OUTPUTS   @{ method; path; query; headers; body } 或 $null
    .NOTES     只支持我们需要的子集：GET/POST/OPTIONS + Content-Length。
               页面只会发小段 JSON，因此不支持 chunked。
  #>
  param([Parameter(Mandatory)]$Stream)

  $reader = New-Object System.IO.StreamReader($Stream, [System.Text.Encoding]::UTF8, $false, 1024, $true)

  $requestLine = $reader.ReadLine()
  if ([string]::IsNullOrWhiteSpace($requestLine)) { return $null }

  $parts = $requestLine.Split(' ')
  if ($parts.Length -lt 2) { return $null }
  $method = $parts[0].ToUpperInvariant()
  $target = $parts[1]

  $headers = @{}
  while ($true) {
    $line = $reader.ReadLine()
    if ($null -eq $line -or $line -eq '') { break }
    $idx = $line.IndexOf(':')
    if ($idx -gt 0) {
      $name = $line.Substring(0, $idx).Trim().ToLowerInvariant()
      $value = $line.Substring($idx + 1).Trim()
      $headers[$name] = $value
    }
  }

  $body = ''
  if ($headers.ContainsKey('content-length')) {
    $len = 0
    if ([int]::TryParse($headers['content-length'], [ref]$len) -and $len -gt 0) {
      if ($len -gt 4194304) { return @{ error = 'payload too large' } }   # 4MB
      $buf = New-Object char[] $len
      $read = 0
      while ($read -lt $len) {
        $n = $reader.Read($buf, $read, $len - $read)
        if ($n -le 0) { break }
        $read += $n
      }
      if ($read -gt 0) { $body = -join $buf[0..($read - 1)] }
    }
  }

  $path = $target
  $query = ''
  $qi = $target.IndexOf('?')
  if ($qi -ge 0) {
    $path = $target.Substring(0, $qi)
    $query = $target.Substring($qi + 1)
  }

  return @{
    method  = $method
    path    = $path
    query   = $query
    headers = $headers
    body    = $body
  }
}

function Get-QueryValue {
  param([string]$Query, [string]$Name)
  if (-not $Query) { return $null }
  foreach ($pair in $Query.Split('&')) {
    $kv = $pair.Split('=', 2)
    if ($kv.Length -eq 2 -and $kv[0] -eq $Name) {
      return [System.Uri]::UnescapeDataString($kv[1])
    }
  }
  return $null
}

function Test-RequestOrigin {
  <# 校验 token；若带 Origin 头则必须与本机源一致 #>
  param([Parameter(Mandatory)]$Request)

  $token = Get-QueryValue -Query $Request.query -Name 't'
  if ($token -ne $script:DtsToken) { return $false }

  $origin = $null
  if ($Request.headers.ContainsKey('origin')) { $origin = $Request.headers['origin'] }
  if (-not $origin) { return $true }
  # 允许 127.0.0.1 与 localhost 两种写法
  return ($origin -eq "http://127.0.0.1:$($script:DtsPort)") -or
         ($origin -eq "http://localhost:$($script:DtsPort)")
}

# ------------------------------------------------------------------ 状态回传

function Add-HostError {
  param([Parameter(Mandatory)][string]$Message)
  $list = [System.Collections.ArrayList]@($script:DtsResp.recentErrors)
  [void]$list.Add($Message)
  while ($list.Count -gt 5) { $list.RemoveAt(0) }
  $script:DtsResp.recentErrors = @($list)
  Write-DtsLog $Message
}

function Set-HostCaps {
  param([Parameter(Mandatory)][hashtable]$Caps)
  $script:DtsResp.caps = $Caps
}

function Set-HostState {
  param([Parameter(Mandatory)][hashtable]$State)
  $script:DtsResp.state = $State
}

function Set-HostBounds {
  param([AllowNull()]$Bounds, [AllowNull()]$Hwnd)
  if ($null -ne $Bounds) { $script:DtsResp.bounds = $Bounds }
  if ($null -ne $Hwnd)   { $script:DtsResp.hwnd = [string]$Hwnd }
}

function Get-PageSeenAt { return $script:DtsPageSeenAt }

function Publish-HostResponse {
  $script:DtsResp.at = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}

function Receive-HostCommand {
  <#
    取出页面下发的全部指令并清空队列。
    [CmdletBinding()] 是必需的：函数带该属性时 PowerShell 不会把返回的
    单元素/空数组"解包"成标量，调用方才能稳定地用 @(...) 处理结果。
  #>
  [CmdletBinding()]
  param()
  if ($script:DtsQueue.Count -eq 0) { return @() }
  $items = @($script:DtsQueue)
  $script:DtsQueue.Clear()
  return $items
}

# ------------------------------------------------------------------ 路由

function Invoke-HostApi {
  param(
    [Parameter(Mandatory)]$Request,
    [Parameter(Mandatory)]$Stream,
    [Parameter(Mandatory)][string]$SubPath
  )

  if (-not (Test-RequestOrigin -Request $Request)) {
    Send-Http -Stream $Stream -StatusCode 403 -Text 'forbidden'
    return
  }

  $script:DtsPageSeenAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()

  switch ($SubPath) {
    'cmd' {
      $count = 0
      if ($Request.body) {
        try {
          $arr = $Request.body | ConvertFrom-Json
          foreach ($item in @($arr)) {
            [void]$script:DtsQueue.Add($item)
            $count++
          }
        } catch {
          Add-HostError "指令 JSON 解析失败：$($_.Exception.Message)"
        }
      }
      # 积压保护：宿主没在消费时丢弃最旧的指令，避免无界增长
      while ($script:DtsQueue.Count -gt 400) { $script:DtsQueue.RemoveAt(0) }
      Send-Http -Stream $Stream -ContentType 'application/json; charset=utf-8' `
        -Text (@{ ok = $true; accepted = $count } | ConvertTo-Json -Compress)
    }

    'resp' {
      Publish-HostResponse
      Send-Http -Stream $Stream -ContentType 'application/json; charset=utf-8' `
        -Text ($script:DtsResp | ConvertTo-Json -Compress -Depth 5)
    }

    'ping' {
      Send-Http -Stream $Stream -ContentType 'application/json; charset=utf-8' `
        -Text (@{ ok = $true; at = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() } | ConvertTo-Json -Compress)
    }

    default {
      Send-Http -Stream $Stream -StatusCode 404 -Text 'not found'
    }
  }
}

function Invoke-StaticFile {
  param(
    [Parameter(Mandatory)]$Request,
    [Parameter(Mandatory)]$Stream
  )

  if ($Request.method -ne 'GET' -and $Request.method -ne 'HEAD') {
    Send-Http -Stream $Stream -StatusCode 405 -Text 'method not allowed'
    return
  }

  $file = Resolve-SitePath -UrlPath $Request.path
  if (-not $file) {
    Send-Http -Stream $Stream -StatusCode 400 -Text 'bad request'
    return
  }

  if (Test-Path -LiteralPath $file -PathType Container) {
    $file = Join-Path $file 'index.html'
  }

  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
    Send-Http -Stream $Stream -StatusCode 404 -Text "404 not found: $($Request.path)"
    return
  }

  $ext = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
  $ctype = if ($script:DtsMimeMap.ContainsKey($ext)) { $script:DtsMimeMap[$ext] } else { 'application/octet-stream' }

  try {
    $bytes = [System.IO.File]::ReadAllBytes($file)
  } catch {
    Send-Http -Stream $Stream -StatusCode 500 -Text "读取文件失败：$($_.Exception.Message)"
    return
  }

  Send-Http -Stream $Stream -ContentType $ctype -Body $bytes
}

# ------------------------------------------------------------------ 非阻塞泵

function Invoke-ServerStep {
  <#
    .SYNOPSIS  若有已就绪的连接则处理之，否则立即返回（绝不阻塞）
    .OUTPUTS   $true 表示服务器仍在运行
  #>
  if (-not $script:DtsListener) { return $false }
  if (-not $script:DtsPending) {
    if (-not (Start-ServerAsyncWait)) { return $false }
  }

  # 没有已完成的连接就立刻返回，把时间让给窗口消息与指令处理
  if (-not $script:DtsPending.AsyncWaitHandle.WaitOne(0)) { return $true }

  $async = $script:DtsPending
  $script:DtsPending = $null

  $client = $null
  try { $client = $script:DtsListener.EndAcceptTcpClient($async) } catch { $client = $null }

  # 立刻发起下一次 accept，保证连接不被漏掉
  [void](Start-ServerAsyncWait)
  if ($null -eq $client) { return $true }

  $stream = $null
  try {
    # 短超时：避免半开连接把主循环拖住
    $client.ReceiveTimeout = 3000
    $client.SendTimeout = 3000
    $stream = $client.GetStream()

    $req = Read-HttpRequest -Stream $stream
    if ($null -eq $req) { return $true }
    # StrictMode 下访问不存在的哈希键会抛异常，必须用 ContainsKey 判断
    if ($req.ContainsKey('error')) {
      Send-Http -Stream $stream -StatusCode 400 -Text $req.error
      return $true
    }

    if ($req.path -like '/__host/*') {
      $sub = $req.path.Substring('/__host/'.Length).Trim('/')
      if (-not $sub) { $sub = 'ping' }
      Invoke-HostApi -Request $req -Stream $stream -SubPath $sub
      return $true
    }

    # 预检请求（JSON POST 会触发 CORS preflight）
    if ($req.method -eq 'OPTIONS') {
      Send-Http -Stream $stream -StatusCode 200 -Text ''
      return $true
    }

    Invoke-StaticFile -Request $req -Stream $stream
  } catch {
    Write-DtsLog "处理请求异常：$($_.Exception.Message)"
    try { Send-Http -Stream $stream -StatusCode 500 -Text 'internal error' } catch { }
  } finally {
    try { if ($stream) { $stream.Close() } } catch { }
    try { $client.Close() } catch { }
  }
  return $true
}
