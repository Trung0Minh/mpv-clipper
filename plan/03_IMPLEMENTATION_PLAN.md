# 03 — Detailed Implementation Plan

This document is ordered for a coding agent. Complete phases in sequence. Do not begin V2 features during V1 implementation.

---

# Phase 0 — Repository and build skeleton

## Tasks

1. Create C++20 Qt 6/CMake project.
2. Add dependency discovery for:
   - Qt6::Core
   - Qt6::Widgets
   - Qt6::Gui
   - Qt6::OpenGL / OpenGLWidgets as required
   - libmpv
3. Create the source tree described in `README.md`.
4. Add logging categories:
   - app
   - mpv
   - ffprobe
   - export
   - ui
5. Add a basic CI build if repository environment permits.
6. Add command line parser for:
   - `--launch-payload <file>`
   - `--source <file>` for development
   - `--time <seconds>` for development
7. Add placeholder MainWindow.

## Exit criteria

- application starts
- empty MainWindow appears
- command line parsing works
- build is reproducible with CMake

---

# Phase 1 — mpv launcher bridge

## Tasks

### 1.1 Lua script

Create `mpv/clipper.lua`.

Implement:

- configurable executable path
- named hotkey/default hotkey
- read source and current playback context
- reject no-file state
- reject unsupported source if V1 requires local files
- serialize launch JSON
- write to temporary file
- remember original pause state
- set original mpv pause to true
- launch app asynchronously
- show OSD launch error if process fails
- restore previous pause state when child process finishes
- clean up temp payload

### 1.2 Quoting/security

Never build one shell command string.
Use argument arrays/native subprocess API.

### 1.3 Duplicate invocation

Allow the GUI's single-instance layer to decide focus behavior. Lua may launch a tiny second process; it should exit immediately after handing payload to the primary app.

## Exit criteria

- pressing hotkey launches placeholder GUI
- path containing spaces/Unicode works
- mpv pauses on open
- closing GUI restores original pause/play state

---

# Phase 2 — Launch payload and single-instance IPC

## Tasks

1. Define `LaunchPayload` schema and parser.
2. Validate schema version.
3. Implement `SingleInstance` with QLocalServer/QLocalSocket.
4. On secondary invocation:
   - send payload bytes to primary instance
   - request raise/focus
   - exit
5. On primary receipt:
   - open/replace media session if safe
6. Delete consumed payload file.
7. Add defensive parsing and error dialogs.

## Exit criteria

- one Clipper window only
- repeated hotkeys focus existing app
- new payload can be delivered to running app

---

# Phase 3 — ffprobe media model

## Tasks

1. Implement `MediaProbe` using `QProcess`.
2. Parse JSON output.
3. Build models:
   - MediaInfo
   - VideoStream
   - AudioTrack
   - SubtitleTrack
4. Parse rational FPS values safely.
5. Parse language/title/disposition metadata.
6. Expose probe errors with useful stderr detail.
7. Add unit tests using small fixture metadata JSON if binary fixtures are undesirable.

## Exit criteria

- source duration/resolution/tracks appear correctly in development logs/UI
- malformed or unsupported file produces a useful error

---

# Phase 4 — Embedded libmpv preview

## Tasks

### 4.1 Player wrapper

Implement `MpvPlayer`:

- create/destroy mpv handle
- set preview-specific options before initialize
- observe time-pos/pause/duration/track-list
- execute async commands
- load file
- seek exact
- play/pause
- set aid/sid
- disable audio
- disable subtitle visibility
- frame-step/frame-back-step

### 4.2 Render widget

Implement `MpvRenderWidget` using libmpv render API.

Requirements:

- Qt-controlled rendering context
- resize-safe
- high-DPI-safe
- no external mpv child window
- clean shutdown

### 4.3 Initial load behavior

When file loaded:

- pause
- obtain duration
- seek to launch timestamp
- match original audio/subtitle track
- emit ready state

## Exit criteria

- video renders inside Qt
- seeking works
- play/pause works
- audio works
- subtitle display works
- same file can be opened from mpv hotkey

---

# Phase 5 — Main UI shell

## Tasks

Build the permanent layout:

- preview panel
- right settings panel
- timeline section
- transport controls
- Start/End fine controls
- filename/save controls
- export/status area

Requirements:

- sensible default size around 1100×720
- minimum size
- resizable
- remember geometry
- no fullscreen default
- mouse controls visible

Use temporary controls where later phases replace behavior.

## Exit criteria

- full V1 layout visible
- resizing behaves correctly
- settings panel does not consume excessive preview area

---

# Phase 6 — Range timeline

## Tasks

### 6.1 Custom widget

Implement `RangeTimeline` with:

- Start handle
- End handle
- playhead
- selected highlight
- unselected dim regions
- mouse hit testing
- drag modes
- click-to-seek

### 6.2 Initialization

On media load:

```text
Start = 0
End = duration
Playhead = launch timestamp
```

### 6.3 Drag behavior

- clamp Start and End
- never allow Start >= End
- boundary dragging pauses preview
- seek preview to moved boundary
- throttle continuous seeks while dragging
- exact seek on mouse release

### 6.4 Playhead sync

Bind player `time-pos` to timeline playhead.
Do not create feedback loops while user is actively dragging.

## Exit criteria

- entire bar highlighted initially
- both handles are easy to drag
- selected region updates correctly
- click and scrub work

---

# Phase 7 — Selection-constrained playback and Loop

## Tasks

1. Implement Play/Pause button.
2. Implement Loop toggle.
3. Enforce playback range.
4. If Play pressed outside range, seek Start.
5. At End:
   - loop ON -> seek Start and continue
   - loop OFF -> pause at End
6. If Start/End is changed while playing, pause during edit.
7. If range changes and playhead is now outside range, clamp playhead.

## Exit criteria

- preview never intentionally continues beyond selected End
- Loop is stable over repeated cycles
- very short clips behave correctly

---

# Phase 8 — Timestamp input and frame stepping

## Tasks

### 8.1 Timecode utility

Implement parsing/formatting:

```text
SS.mmm
MM:SS.mmm
HH:MM:SS.mmm
```

### 8.2 Editable fields

- validate on commit
- normalize formatting
- show invalid input state without crashing
- update timeline/player after valid edit

### 8.3 Frame-step buttons

For each boundary:

- previous frame
- next frame

Use libmpv's actual frame stepping, read resulting time-pos, then assign marker.

Handle:

- beginning/end of file
- Start/End collision
- asynchronous step completion

## Exit criteria

- frame-step visibly moves markers to real decoded frame times
- direct timestamp entry stays synchronized with timeline

---

# Phase 9 — Audio/subtitle track UI

## Tasks

### 9.1 Track dropdowns

Populate from media models.

Audio:

```text
None
Track 1...
Track 2...
```

Subtitle track selector separately lists available subtitle tracks.

### 9.2 Initial track match

Use launch-payload hint, then embedded mpv track data.

### 9.3 Live preview

Audio selection:

- select mpv aid
- None -> mute/disable audio

Subtitle selection:

- select mpv sid
- None mode -> hide subtitles
- Hardsub/Softsub -> show selected subtitle

### 9.4 Subtitle mode compatibility

Mode dropdown dynamically reflects selected output format and selected subtitle codec.

## Exit criteria

- changing audio while previewing changes sound immediately
- changing subtitle changes displayed subtitle immediately
- GIF never shows Softsub option

---

# Phase 10 — Output settings UI

## Tasks

### Video

Implement:

- Format: MP4/MKV/WebM
- Resolution: Original/1080p/720p/480p/Custom
- Quality: High/Medium/Low
- FPS: Original/60/30/24
- Subtitle mode/track
- Audio track

### GIF

Switch panel to:

- Width
- FPS
- Quality
- Subtitle None/Hardsub

Hide audio.

### Dynamic behavior

Settings changes trigger validation but not expensive preview re-rendering.

## Exit criteria

- format transitions never leave stale invalid controls
- changing back from GIF restores video settings

---

# Phase 11 — Filename and destination

## Tasks

1. Implement default export directory strategy.
2. Generate filename from source + range.
3. Track `filenameManuallyEdited`.
4. Regenerate automatically only before manual edit.
5. Update extension when output format changes.
6. Implement Browse directory picker.
7. Sanitize names.
8. Detect existing target file.

## Exit criteria

- range changes update automatic filename
- manual name is respected
- extension always matches chosen format

---

# Phase 12 — Export command builder

Implement `ExportCommandBuilder` as a pure/testable component.

Inputs:

- MediaInfo
- Start/End
- selected tracks
- subtitle mode
- output format
- resolution/FPS/quality settings
- destination

Output:

```text
program = ffmpeg
arguments = [...]
```

Do not concatenate a shell string.

Implement format rules from `04_EXPORT_PIPELINE.md`.

Add unit tests for representative combinations.

## Exit criteria

- generated argument arrays match expected behavior
- unsupported combinations return validation errors instead of commands

---

# Phase 13 — Export process and progress UI

## Tasks

1. Start FFmpeg with QProcess.
2. Use machine-readable progress output.
3. Parse `out_time`/`out_time_ms`.
4. Compute percentage against selected clip duration.
5. Add progress bar.
6. Add Cancel.
7. Capture bounded diagnostic stderr.
8. On nonzero exit:
   - preserve session
   - show friendly error
   - offer technical details
9. On success:
   - mark 100%
   - show Open File / Open Folder / Copy Path

## Exit criteria

- UI remains responsive throughout export
- cancel stops process and removes partial output
- success actions work

---

# Phase 14 — Format-specific validation

## Tasks

### MP4

- H.264/AAC default
- Softsub allowed only for compatible text subtitle streams convertible to mov_text
- otherwise disable Softsub

### MKV

- allow broad subtitle preservation/re-encoding
- H.264/AAC default for predictable output

### WebM

- VP9/Opus
- Softsub only when convertible to WebVTT
- otherwise Hardsub/None

### GIF

- no audio
- no Softsub
- palette generation pipeline

### General

Probe FFmpeg feature availability and disable unavailable formats rather than failing late.

## Exit criteria

- user cannot start known-invalid exports
- common ASS/SRT workflows work correctly

---

# Phase 15 — Polish and error handling

## Tasks

Handle:

- source deleted after launch
- source not readable
- ffprobe failure
- FFmpeg missing
- libmpv initialization failure
- no video stream
- missing selected track
- output directory permission failure
- disk write failure
- zero/unknown duration
- corrupted media
- Unicode paths
- very long filenames
- high-DPI display
- display scale changes

Add tooltips for Hardsub/Softsub if useful.

Do not expose raw FFmpeg jargon as the primary error message.

## Exit criteria

- errors are understandable
- app never silently fails
- session is preserved after recoverable export errors

---

# Phase 16 — Packaging and installation

## Linux V1

Provide:

- application binary/package
- mpv Lua script
- example mpv script config
- installation instructions

Document dependencies:

- Qt runtime if not bundled
- libmpv
- ffmpeg/ffprobe

Optional later packaging:

- AppImage
- Flatpak

Be careful with Flatpak sandboxing because the app must access arbitrary source/export files and communicate smoothly with the host mpv workflow.

## Exit criteria

A clean machine following the documented setup can invoke Clipper from mpv and export a clip.

---

# Phase 17 — V1 regression pass

Run the full matrix in `05_TESTING_ACCEPTANCE.md`.

Do not mark V1 complete until:

- core acceptance criteria pass
- no P0/P1 bugs remain
- export outputs have been manually opened and checked
- Start/End timing has been compared against expected frames on representative files

After V1 is stable, proceed to the ideas in `06_V2_NOTES.md`.
