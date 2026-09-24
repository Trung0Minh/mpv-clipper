mpv Clipper - portable release

1. Install mpv with Lua support separately.
2. Extract this package into a permanent, writable folder. Do not run from inside
   a ZIP viewer or move the folder after setup.
3. Linux: run sh install.sh (extracts the AppImage once; no FUSE required).
   Windows: double-click install.cmd.
   macOS: double-click install.command.
4. Restart mpv, open a local video, and press Ctrl+Shift+X.

No Conda, compiler, administrator account, or separately installed FFmpeg is needed.
FFmpeg, ffprobe, Qt and libmpv are included in release packages.

Custom mpv configuration: run the setup script from a terminal with
  --mpv-config "/path/to/mpv/config"
Use the same option when uninstalling. On Windows, portable_config next to an mpv
on PATH is detected; otherwise the default is %APPDATA%\mpv. If your portable mpv
is not on PATH, supply its portable_config path explicitly. Unix defaults to
$XDG_CONFIG_HOME/mpv or ~/.config/mpv. Nothing edits input.conf or mpv.conf.

To upgrade: close Clipper, extract the new release to a new permanent folder and
run its installer using the same mpv configuration directory. You can then delete
the old package. Do not run the old uninstaller after upgrading.

To remove: close Clipper, run this package's uninstall script (same custom config
option, if used), then delete the package folder. Pre-existing clipper.lua and
clipper.conf are restored. If you edited managed files after setup, removal stops
to protect those edits; the original/installed contents are stored as base64 in
script-opts/mpv-clipper-install.json. Back up your edits before resolving a conflict.

Unsigned Windows/macOS releases can show operating-system security prompts.
Use the operating system's per-app approval after verifying the download; do not
disable system security globally. Signing/notarization requires maintainer keys.

See RELEASE.md for supported builds, validation status, and publishing requirements.
