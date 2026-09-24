@echo off
"%~dp0mpv-clipper.exe" --install-mpv %*
if errorlevel 1 (
    echo Setup failed. See the message above.
    pause
    exit /b 1
)
echo Keep this folder in place. Restart mpv and press Ctrl+Shift+X.
pause
