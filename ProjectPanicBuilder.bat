@echo off
rem Project Panic Builder: opens the build window inside WSL (Windows 11, or Windows 10 with WSLg).
cd /d "%~dp0"
wsl --cd "%~dp0." sh tools/builder.sh
if errorlevel 1 pause
