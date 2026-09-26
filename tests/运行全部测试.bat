@echo off
setlocal
set "TEST_ROOT=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%TEST_ROOT%run-all.ps1"
set "TEST_EXIT=%ERRORLEVEL%"
endlocal & exit /b %TEST_EXIT%
