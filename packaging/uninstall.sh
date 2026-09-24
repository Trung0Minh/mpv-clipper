#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case $(uname -s) in
    Linux) launcher="$base/squashfs-root/AppRun" ;;
    Darwin) launcher="$base/mpv-clipper.app/Contents/MacOS/mpv-clipper" ;;
    *) echo "Use uninstall.cmd on Windows." >&2; exit 1 ;;
esac
"$launcher" --uninstall-mpv "$@"
printf '\nmpv integration removed. You can now delete this package folder.\n'
if [ -t 0 ]; then printf 'Press Enter to close. '; read -r answer; fi
