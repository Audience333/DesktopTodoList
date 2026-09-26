@echo off
rem ============================================================================
rem  启动待办.bat — 双击入口
rem ----------------------------------------------------------------------------
rem  优先启动桌面版（PowerShell 宿主 + Edge 浮窗）。
rem  若宿主启动失败，自动回退到浏览器模式打开 src\index.html，
rem  保证用户在任何环境下都至少能用上任务功能。
rem ============================================================================
chcp 65001 >nul 2>&1
setlocal

set "ROOT=%~dp0"
set "LAUNCHER=%ROOT%host\launcher.ps1"

if not exist "%LAUNCHER%" (
  echo [错误] 找不到启动脚本：
  echo        %LAUNCHER%
  echo.
  echo 请确认 host\launcher.ps1 是否存在，或直接双击 src\index.html 使用浏览器模式。
  pause
  exit /b 1
)

rem 用 -WindowStyle Hidden 隐藏宿主自身的控制台窗口；
rem 宿主启动失败时会自己弹提示，不依赖这个窗口。
powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%LAUNCHER%" %*
set "RC=%ERRORLEVEL%"

rem 退出码 1 且非用户主动退出时，给一次浏览器模式兜底
if "%RC%"=="1" (
  echo.
  echo 桌面模式未能启动，正在以浏览器模式打开…
  start "" "%ROOT%src\index.html"
)
endlocal
