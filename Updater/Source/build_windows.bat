@echo off
REM Run this ON A WINDOWS MACHINE with Python 3 installed.
REM Produces dist\AutoChangerDeployTool.exe
REM
REM The build itself happens in your TEMP folder rather than here, so a file-sync
REM client (e.g. Seafile) or antivirus can't lock half-built files. Only the
REM finished exe is copied into dist\.

REM A still-running copy of the tool keeps its exe locked ("Access is denied").
taskkill /f /im AutoChangerDeployTool.exe >nul 2>&1

set BUILD=%TEMP%\AutoChangerBuild

python -m pip install --upgrade pip
python -m pip install -r requirements.txt
python -m pip install pyinstaller

python -m PyInstaller --noconfirm --clean --onefile --windowed --collect-data esptool --name AutoChangerDeployTool --workpath "%BUILD%\work" --distpath "%BUILD%\dist" --specpath "%BUILD%" deploy_tool.py
if errorlevel 1 (
    echo.
    echo BUILD FAILED - see the error above.
    pause
    exit /b 1
)

if not exist dist mkdir dist
copy /y "%BUILD%\dist\AutoChangerDeployTool.exe" "dist\AutoChangerDeployTool.exe"
if errorlevel 1 (
    echo.
    echo Built OK, but could not copy into dist\ - something is still locking the old exe
    echo ^(a running copy, antivirus, or sync client^). Wait a few seconds and run this again,
    echo or grab the new exe directly from:
    echo   %BUILD%\dist\AutoChangerDeployTool.exe
    pause
    exit /b 1
)

echo.
echo Done. The executable is in dist\AutoChangerDeployTool.exe
pause
