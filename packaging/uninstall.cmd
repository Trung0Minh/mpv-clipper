@echo off
"%~dp0mpv-clipper.exe" --uninstall-mpv %*
if errorlevel 1 (
    echo Removal failed. See the message above.
    pause
    exit /b 1
)
echo mpv integration removed. You can now delete this package folder.
pause
