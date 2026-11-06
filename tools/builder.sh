#!/bin/sh
# Starts Project Panic Builder (tools/builder_gui.py). Used by ProjectPanicBuilder.bat on Windows (through WSL).
cd "$(dirname "$0")/.."
if ! python3 -c "import tkinter" 2>/dev/null; then
  echo "The builder window needs Tk for Python:  sudo apt install python3-tk"
  exit 1
fi
exec python3 tools/builder_gui.py
