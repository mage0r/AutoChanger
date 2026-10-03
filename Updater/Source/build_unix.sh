#!/bin/bash
# Run this ON A MAC OR LINUX MACHINE with Python 3 installed.
# Produces dist/AutoChangerDeployTool (Linux) or dist/AutoChangerDeployTool.app (Mac)
set -e

python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt
python3 -m pip install pyinstaller

python3 -m PyInstaller --onefile --windowed --collect-data esptool --name AutoChangerDeployTool deploy_tool.py

echo ""
echo "Done. The executable is in dist/AutoChangerDeployTool"
