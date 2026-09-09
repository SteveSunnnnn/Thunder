@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0validate_thunder.ps1"
exit /b %errorlevel%
