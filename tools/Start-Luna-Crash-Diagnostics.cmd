@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Luna-Crash-Diagnostics.ps1"
if errorlevel 1 pause
