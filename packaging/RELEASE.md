# Release builds

The `Portable release candidates` GitHub Actions workflow builds Linux x86-64,
Windows x86-64, and separate Apple Silicon/Intel macOS packages. Run it manually
or push a version tag. Each job builds and tests on its native platform, deploys
runtime libraries and FFmpeg/ffprobe, and uploads an archive. Successful version-tag
builds create a **draft** GitHub Release with packages, project source and SHA-256
checksums. Artifacts are release candidates, not automatically public releases.

Linux builds target Ubuntu 24.04 or compatible/newer glibc systems. An AppImage
does not make a glibc 2.39 build run on older distributions. Graphics drivers remain
host dependencies. Setup extracts the AppImage once to avoid a FUSE requirement.
Windows uses the MSYS2 UCRT64 toolchain and targets Windows 10/11 x64. macOS
packages target the version/architecture of their runner; do not label them
universal or compatible with older macOS releases without testing those systems.

The app is portable and installed **in place**: put its folder somewhere permanent
before running setup. The user's existing mpv installation still needs Lua support.
No Conda environment is required for the native release packages. The old local
Conda CPack archive is a development artifact and must not be uploaded as one of
these portable packages.

## Publishing

1. Run the workflow and inspect all four platform jobs. Download the artifacts;
   verify a real video, Ctrl+Shift+X, repeated launch, subtitles, frame stepping,
   one export, Multi Clip, cancellation, setup, upgrade and removal on clean systems.
   CI GUI/codec availability can differ from users' machines.
2. Test the Linux AppImage on a compatible clean machine without development
   packages. For macOS, confirm no Homebrew paths remain in the packaged dependency
   tree (`otool -L`); for Windows, test without MSYS2 or Qt on PATH.
3. Windows Authenticode and macOS Developer ID signing/notarization require the
   maintainer's certificates. Current macOS packages are ad-hoc signed only;
   Windows packages are unsigned. Do not advertise them as signed/notarized.
4. Review dependency licenses and include exact corresponding sources/build
   instructions for redistributed GPL components, plus LGPL relinking requirements
   where applicable. The package contains license notices and a dependency-version
   manifest, but those alone do **not** satisfy corresponding-source obligations.
   Preserve package-manager source recipes and patches for the exact builds used.
   Do not publish binaries until their applicable redistribution requirements are met.
5. Create a GitHub Release with the verified archives, project source archive,
   dependency source material and SHA-256 checksums. Include platform requirements
   and setup/removal instructions from the package README. Keep older downloads
   available so users can roll back by reinstalling an older package.

Deployment tools currently use linuxdeploy's `continuous` downloads and the native
package managers' current versions. Each package records dependency versions;
before a stable release, pin/archive the exact deployment tools and build inputs.

## Current verification

Linux compilation, installer backup/restore and conflict protection, startup,
bridge behavior, decoding/frame stepping and real export regressions are locally
testable. Windows/macOS deployment and clean-machine package checks require the
native GitHub jobs and platform acceptance runs; adding the workflow is not proof
that those packages have passed. No signed binaries are produced locally.

## Building manually

Install the dependencies listed in `.github/workflows/release.yml`, then:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 3
ctest --test-dir build-release --output-on-failure
bash packaging/package.sh
```

Run Windows commands in an MSYS2 UCRT64 shell. On macOS, add Homebrew Qt's prefix
to `CMAKE_PREFIX_PATH`. Linux packaging uses system libraries, not Conda. Packaging
expects a fresh `dist/` output directory. The build workflow archives Unix packages
as `.tar.gz` to retain executable permissions and Windows packages as `.zip`.
