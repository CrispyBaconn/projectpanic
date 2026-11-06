@echo off
rem Project Panic Builder: opens the build window. Needs Python 3 for Windows (python.org), no WSL needed.
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
  py -3 tools\builder_gui.py
  if errorlevel 1 pause
  goto :eof
)
where python >nul 2>nul
if not errorlevel 1 (
  python tools\builder_gui.py
  if errorlevel 1 pause
  goto :eof
)
echo Python 3 is needed: https://www.python.org/downloads/  (tick "Add python.exe to PATH" while installing)
pause
