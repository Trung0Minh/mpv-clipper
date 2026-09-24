#!/usr/bin/env bash
set -euo pipefail
# Run from the source root after a native Release build in build-release/.
root=$PWD
mkdir -p dist
stage="$root/dist/mpv-clipper-$(uname -s)-$(uname -m)"
mkdir "$stage"
cp LICENSE README.md packaging/README.txt packaging/RELEASE.md "$stage/"
mkdir -p "$stage/licenses"
case $(uname -s) in
Linux)
    appdir="$root/build-release/AppDir"
    cmake --install build-release --prefix "$appdir/usr"
    cp /usr/bin/ffmpeg /usr/bin/ffprobe "$appdir/usr/bin/"
    mkdir -p "$appdir/usr/share/icons/hicolor/scalable/apps"
    cp assets/mpv-clipper.svg "$appdir/usr/share/icons/hicolor/scalable/apps/"
    # Deployment tools run extracted, so the CI worker does not need FUSE.
    tools="$root/build-release/deploy-tools"
    mkdir -p "$tools"
    for tool in linuxdeploy linuxdeploy-plugin-qt; do
        curl --fail --location --retry 3 "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage" -o "$tools/$tool.AppImage"
        chmod +x "$tools/$tool.AppImage"
        mkdir "$tools/$tool"
        (cd "$tools/$tool" && ../"$tool.AppImage" --appimage-extract >/dev/null)
        printf '#!/bin/sh\nexec "%s" "$@"\n' "$tools/$tool/squashfs-root/AppRun" > "$tools/$tool-run"
        chmod +x "$tools/$tool-run"
    done
    mkdir -p "$tools/bin"
    ln -s "$tools/linuxdeploy-plugin-qt-run" "$tools/bin/linuxdeploy-plugin-qt"
    export PATH="$tools/bin:$PATH" QMAKE=/usr/bin/qmake6 ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1
    export OUTPUT="$stage/mpv-clipper.AppImage"
    "$tools/linuxdeploy-run" --appdir "$appdir" --plugin qt \
        --executable "$appdir/usr/bin/ffmpeg" --executable "$appdir/usr/bin/ffprobe" --output appimage
    cp packaging/install.sh packaging/uninstall.sh "$stage/"
    dpkg-query -W > "$stage/dependencies.txt"
    for doc in /usr/share/doc/*/copyright; do
        name=$(basename "$(dirname "$doc")")
        cp "$doc" "$stage/licenses/$name.txt"
    done
    ;;
Darwin)
    brew_prefix=$(brew --prefix)
    cp -R build-release/mpv-clipper.app "$stage/"
    app="$stage/mpv-clipper.app"
    cp "$brew_prefix/bin/ffmpeg" "$brew_prefix/bin/ffprobe" "$app/Contents/MacOS/"
    "$(brew --prefix qt)/bin/macdeployqt" "$app" \
        -executable="$app/Contents/MacOS/ffmpeg" -executable="$app/Contents/MacOS/ffprobe" \
        -libpath="$brew_prefix/lib" -always-overwrite
    # macdeployqt handles Qt plugins; dylibbundler closes the non-Qt dependency tree.
    dylibbundler -od -b -x "$app/Contents/MacOS/mpv-clipper" \
        -x "$app/Contents/MacOS/ffmpeg" -x "$app/Contents/MacOS/ffprobe" \
        -d "$app/Contents/Libraries" -p '@executable_path/../Libraries/' \
        -s "$brew_prefix/lib" -i "$app/Contents/Frameworks"
    codesign --force --deep --sign - "$app"
    codesign --verify --deep --strict "$app"
    cp packaging/install.sh "$stage/install.command"
    cp packaging/uninstall.sh "$stage/uninstall.command"
    brew info --json=v2 --installed > "$stage/dependencies.json"
    while IFS= read -r file; do
        relative=${file#"$brew_prefix/Cellar/"}
        mkdir -p "$stage/licenses/$(dirname "$relative")"
        cp "$file" "$stage/licenses/$relative"
    done < <(find "$brew_prefix/Cellar" -type f \( -iname '*license*' -o -iname '*copying*' \))
    ;;
MINGW*|MSYS*)
    cp build-release/mpv-clipper.exe "$stage/"
    cp "$MINGW_PREFIX/bin/ffmpeg.exe" "$MINGW_PREFIX/bin/ffprobe.exe" "$stage/"
    windeployqt --release --no-translations "$stage/mpv-clipper.exe"
    # Include the installed runtime DLLs, including FFmpeg/libmpv's dependencies.
    cp "$MINGW_PREFIX"/bin/*.dll "$stage/"
    cp -R "$MINGW_PREFIX/share/licenses/." "$stage/licenses/"
    cp packaging/install.cmd packaging/uninstall.cmd "$stage/"
    pacman -Q > "$stage/dependencies.txt"
    ;;
*) echo "Unsupported build platform" >&2; exit 1 ;;
esac
find "$stage" -type f \( -name '*.sh' -o -name '*.command' \) -exec chmod +x {} +
printf '%s\n' "$stage" > build-release/package-path.txt
