@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Analyze-Luna-Crash.ps1"
if errorlevel 1 pause
