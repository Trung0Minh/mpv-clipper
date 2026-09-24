#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case $(uname -s) in
    Linux)
        if [ ! -x "$base/squashfs-root/AppRun" ]; then
            chmod +x "$base/mpv-clipper.AppImage"
            (cd "$base" && ./mpv-clipper.AppImage --appimage-extract >/dev/null)
        fi
        launcher="$base/squashfs-root/AppRun"
        ;;
    Darwin) launcher="$base/mpv-clipper.app/Contents/MacOS/mpv-clipper" ;;
    *) echo "Use install.cmd on Windows." >&2; exit 1 ;;
esac
MPV_CLIPPER_LAUNCHER="$launcher" "$launcher" --install-mpv "$@"
printf '\nSetup complete. Keep this folder in place. Restart mpv and press Ctrl+Shift+X.\n'
if [ -t 0 ]; then printf 'Press Enter to close. '; read -r answer; fi
