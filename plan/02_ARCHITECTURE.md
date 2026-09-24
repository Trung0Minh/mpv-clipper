# 02 — Architecture

## 1. High-level architecture

```text
┌──────────────────────────┐
│        mpv player        │
│                          │
│  clipper.lua             │
└─────────────┬────────────┘
              │ launch payload
              ▼
┌──────────────────────────────────────────────────────┐
│                   Clipper GUI                        │
│                                                      │
│  Qt UI                                               │
│    │                                                 │
│    ├── AppController                                 │
│    │      ├── MediaProbe (ffprobe)                   │
│    │      ├── MpvPlayer (embedded libmpv)            │
│    │      └── ExportJob (ffmpeg)                     │
│    │                                                 │
│    └── RangeTimeline / Settings / Export status      │
└──────────────────────────────────────────────────────┘
```

The original mpv process and Clipper preview are separate playback contexts using the same source.

---

## 2. mpv Lua bridge responsibilities

The Lua bridge should remain intentionally small.

### Responsibilities

- register the hotkey
- read current mpv properties
- normalize local source path where possible
- store original pause state
- pause original mpv
- launch/focus Clipper asynchronously
- restore original pause state when Clipper exits
- prevent uncontrolled duplicate processes
- surface launch errors via mpv OSD

### Properties to collect

At minimum:

```text
path
media-title
time-pos
pause
aid
sid
track-list
```

Useful optional properties:

```text
playlist-pos
filename
working-directory
```

### Launch payload

Prefer a compact JSON payload written to a temporary file rather than a long shell command line.

Example:

```json
{
  "schemaVersion": 1,
  "source": "/media/anime/movie.mkv",
  "mediaTitle": "movie.mkv",
  "timePos": 754.233,
  "originalPaused": false,
  "activeAudio": {
    "id": 2,
    "lang": "jpn",
    "title": "Japanese",
    "codec": "aac"
  },
  "activeSubtitle": {
    "id": 5,
    "lang": "eng",
    "title": "Dialogue",
    "codec": "ass",
    "external": false,
    "externalFilename": null
  }
}
```

Launch:

```text
mpv-clipper --launch-payload /tmp/mpv-clipper-xxxxx.json
```

Using a payload file avoids quoting bugs with spaces, Unicode, brackets, apostrophes, and long track metadata.

### Async lifecycle

Use mpv's asynchronous subprocess command.

Pseudo-flow:

```text
hotkey
  -> if launch in progress: ignore/focus
  -> remember pause state
  -> set pause=yes
  -> write payload
  -> start Clipper asynchronously
  -> callback when Clipper exits
  -> restore remembered pause state
```

---

## 3. Single-instance application

Only one Clipper window should exist in V1.

Implement with Qt local IPC:

- `QLocalServer`
- `QLocalSocket`

Behavior:

- first process becomes primary instance
- later invocation sends launch payload to primary instance and exits
- primary instance raises/focuses its window

If a different media source is sent while the current window is open:

- if no export is running, replace the loaded session with the new source
- if export is running, keep current session and show a small notification that a new source cannot be opened until export finishes

This prevents multiple editor windows and matches the V1 one-session design.

---

## 4. Core application state

Use one explicit state model instead of letting widgets own business logic.

Example conceptual model:

```cpp
struct ClipSession {
    QString source;
    double durationSec;
    double initialPlayheadSec;

    double startSec;
    double endSec;
    double playheadSec;
    bool loopEnabled;
    bool playing;

    QList<AudioTrack> audioTracks;
    QList<SubtitleTrack> subtitleTracks;
    std::optional<TrackKey> selectedAudio;
    std::optional<TrackKey> selectedSubtitle;
    SubtitleMode subtitleMode;

    OutputFormat format;
    ResolutionPreset resolution;
    QualityPreset quality;
    FpsPreset fps;

    GifSettings gif;

    QString outputDirectory;
    QString outputFilename;
    bool filenameManuallyEdited;

    ExportState exportState;
};
```

Widgets emit intents. `AppController` validates and mutates session state, then widgets render state.

This avoids hidden state divergence between timeline, player and export form.

---

## 5. Embedded libmpv player

### Requirement

Use libmpv's render API inside a Qt-owned rendering surface.

Do not rely on embedding an external mpv OS window by platform-specific window ID as the primary architecture.

### Main responsibilities

`MpvPlayer`:

- initialize mpv handle
- load source
- expose duration/time/pause events
- select audio track
- select subtitle track
- toggle subtitle visibility
- seek accurately
- frame-step forward/backward
- emit file-loaded/error events

`MpvRenderWidget`:

- own OpenGL context/render surface
- bridge Qt repaint/update callbacks to mpv render context
- handle resize/high-DPI

### Recommended mpv options

Before initialization:

- no OSC
- no terminal
- no input default bindings inside preview
- hardware decoding enabled when stable, with software fallback
- start paused
- audio enabled unless user selects None

The preview is controlled by the Qt UI, not mpv's internal on-screen controller.

---

## 6. Preview time synchronization

Observe:

```text
time-pos
pause
duration
```

Update UI playhead at a throttled rate, e.g. 30 Hz maximum.

Do not repaint every property event without throttling.

### End-of-selection guard

The controller enforces `[startSec, endSec]` during preview.

Pseudo-logic:

```text
on timePosChanged(t):
    update playhead

    if playing && t >= endSec - epsilon:
        if loopEnabled:
            seek(startSec, exact)
            play()
        else:
            pause()
            seek(endSec, exact)
```

Choose `epsilon` based on update interval and source characteristics, e.g. 10–30 ms, but ensure the UI ends exactly at the End marker.

---

## 7. Track identity model

Do not assume original mpv track IDs and embedded preview track IDs always match.

Define a stable `TrackKey` from metadata:

```text
type
language
title
codec
external flag
external filename
source order/index
```

Track matching priority:

1. exact external filename
2. language + title + codec + source index
3. language + title + codec
4. source index
5. first default/forced track

The launch payload is only a hint for initial selection.

After loading the source, the embedded libmpv instance's own `track-list` is authoritative for preview.

ffprobe is authoritative for export stream mapping.

---

## 8. ffprobe media inspection

Run ffprobe once when a new source loads.

Recommended JSON query:

```text
-show_format
-show_streams
-print_format json
```

Capture:

- duration
- video stream index
- width/height
- nominal and average frame rate
- pixel format
- audio streams
- subtitle streams
- codec names
- language tags
- title tags
- channel layout
- dispositions

Keep this metadata in `MediaProbeResult`.

Do not run ffprobe repeatedly for every settings change.

---

## 9. Separation between preview and export

Preview settings that must be live:

- clip range
- playhead
- loop
- selected audio track
- selected subtitle track
- subtitle visible/hidden

Export-only settings that do not need live preview:

- MP4/MKV/WebM/GIF format
- output resolution
- output FPS
- quality preset
- Hardsub vs Softsub semantics
- GIF width/quality

This prevents expensive re-rendering and keeps interaction instant.

---

## 10. RangeTimeline widget architecture

Custom Qt widget recommended.

State:

```text
duration
start
end
playhead
hoveredHandle
activeDragMode
```

Drag modes:

```text
None
Playhead
StartHandle
EndHandle
```

Signals:

```text
seekRequested(double)
startPreviewChanged(double)
startCommitted(double)
endPreviewChanged(double)
endCommitted(double)
```

Separate preview/commit signals allow throttled seek during drag and a final exact seek on release.

### Painting

Draw in this order:

1. timeline background
2. unselected regions
3. selected highlighted range
4. playhead
5. Start/End handles
6. optional hover affordances

Use device-independent pixels and Qt scaling APIs.

---

## 11. Frame-step boundary adjustment

Precise frame stepping is player-driven.

For a Start `next frame` action:

1. pause preview
2. seek exactly to current Start
3. request `frame-step`
4. wait for updated `time-pos`
5. assign returned time to Start
6. clamp before End

Previous frame uses `frame-back-step`.

Repeat equivalent logic for End.

Because back-step can require decoder work, disable the button while an individual step request is pending.

---

## 12. Export job architecture

`ExportJob` owns one FFmpeg process using `QProcess`.

Responsibilities:

- build validated command
- start FFmpeg
- parse progress
- emit percentage/status
- support cancellation
- capture stderr to an in-memory bounded log
- report exit status

Never run FFmpeg synchronously on the Qt UI thread.

### Cancellation

1. request graceful termination
2. wait a short bounded interval
3. kill if still running
4. delete incomplete output file unless the user explicitly chose to keep it

---

## 13. Export validation architecture

Before enabling Export, validate:

- source exists and is readable
- duration known
- Start < End
- selected range duration > 0
- output directory exists or can be created
- output filename valid
- output path not source path
- selected audio track exists
- selected subtitle track exists
- format/subtitle combination supported
- FFmpeg executable available
- required FFmpeg filters/encoders available when relevant

Return structured validation issues:

```text
severity
field
userMessage
technicalDetail
```

The settings panel can display errors next to the relevant field.

---

## 14. Configuration

Recommended app config location via Qt `QStandardPaths`.

Persist:

- window geometry
- last export directory
- optional default export directory
- last output format
- last quality preset
- last video resolution preset
- last FPS preset
- last GIF width/FPS/quality
- loop default

Do not persist the previous media source/session.

mpv-side configuration should separately support:

- path to Clipper executable
- launch hotkey
- optional config path

---

## 15. Platform strategy

### Primary V1 target

Linux desktop, including Wayland.

### Cross-platform-friendly design

Avoid Linux-only UI or embedding APIs so Windows/macOS support can be added later.

Platform-specific code should be isolated to:

- locating/opening file manager
- reveal/open output file
- executable discovery
- mpv config path helpers

---

## 16. Dependency checks at startup

Check once per app launch:

- libmpv usable
- ffmpeg found
- ffprobe found

Optional deeper check cached after first run:

- `libx264` encoder
- VP9 encoder (`libvpx-vp9` or selected alternative)
- Opus encoder
- subtitles/libass filter support
- palettegen/paletteuse filters

If a required dependency is missing, disable only affected formats/features when possible instead of making the whole application unusable.
