# mpv Clipper — V1 Implementation Plan

## 1. Goal

Build a mouse-first companion GUI for mpv that lets the user open the current video from mpv, visually select exactly one clip range, preview that range with the desired subtitle/audio track, configure a small set of common output settings, and export the result as a video or GIF.

The application should feel like a built-in mpv clipping tool even though the editor is a separate native window.

V1 is intentionally focused on **one selected segment per editor session**. Multi-segment / batch clipping is reserved for V2.

---

## 2. Core user flow

1. User is watching a local video in mpv.
2. User presses a configurable hotkey, default: `Ctrl+Shift+C`.
3. mpv pauses and opens the Clipper window.
4. Clipper loads the same source into an embedded libmpv preview.
5. The preview playhead starts at the timestamp where the user opened Clipper.
6. The clip selection initially spans the **entire video**:
   - Start = `00:00:00.000`
   - End = full duration
   - Entire timeline selection is highlighted.
7. User edits the clip range with mouse-drag handles, direct timestamp input, or frame-step buttons.
8. User presses Play to preview only the selected range.
9. Optional Loop mode repeats the selected range.
10. User chooses subtitle/audio/output settings.
11. Subtitle/audio changes update preview immediately.
12. User exports the clip.
13. Export progress is shown in the same window.
14. When export finishes, the window stays open and offers:
    - Open File
    - Open Folder
    - Copy Path
15. Closing Clipper restores mpv's previous pause/play state.

---

## 3. V1 scope

### Included

- Launch from mpv hotkey.
- Separate native GUI window.
- Embedded video preview powered by libmpv.
- One clip range only.
- Full-video selection by default.
- Mouse-driven range timeline.
- Start and End markers.
- Selected region highlight.
- Playhead and click-to-seek.
- Drag-to-scrub.
- Start/End frame-step controls.
- Direct editable timestamps.
- Preview playback constrained to selected range.
- Loop on/off.
- Live subtitle switching.
- Live audio switching.
- Subtitle modes:
  - None
  - Hardsub
  - Softsub
- Audio:
  - None
  - One selected audio track
- Output formats:
  - MP4
  - MKV
  - WebM
  - GIF
- Video settings:
  - Resolution
  - Quality
  - FPS
  - Subtitle
  - Audio
- GIF settings:
  - Width
  - FPS
  - Quality
  - None/Hardsub subtitle
- Auto-generated filename.
- User-editable filename.
- User-selectable save folder.
- Export progress.
- Cancel export.
- Open File / Open Folder / Copy Path after export.
- Friendly validation and error messages.
- Configurable hotkey and default export directory.

### Explicitly not included in V1

- Multiple selected ranges in one session.
- Batch export.
- Transition editing.
- Crop editor.
- Video filters/color grading.
- Arbitrary codec tuning UI.
- CRF/preset/bitrate controls exposed to the user.
- Audio mixing or multiple audio tracks.
- Multiple subtitle tracks in one exported clip.
- Live preview of output resolution/FPS/encoding quality.
- Timeline thumbnails/waveforms.
- Network-stream clipping as a guaranteed feature.
- Full video editor behavior.

---

## 4. Recommended stack

### Desktop application

- **C++20**
- **Qt 6 Widgets**
- **CMake**
- **libmpv Render API** for embedded preview
- **FFmpeg + ffprobe CLI** for export and media inspection

### mpv integration

- Small **Lua script** installed into mpv's `scripts/` directory.
- Lua script collects the current source and playback context and launches/focuses Clipper.
- Lua owns the mpv-side pause/restore lifecycle.

### Why this stack

The editor needs reliable native mouse UI, an embedded mpv-quality preview, instant audio/subtitle switching, precise seeking, and frame stepping. Qt + libmpv provides these without relying on fragile child-window embedding tricks. FFmpeg remains the dedicated export engine.

---

## 5. Project file map

```text
mpv-clipper/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── assets/
│   └── icons/
├── mpv/
│   ├── clipper.lua
│   └── clipper.conf.example
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── AppController.*
│   │   ├── SingleInstance.*
│   │   └── LaunchPayload.*
│   ├── media/
│   │   ├── MpvPlayer.*
│   │   ├── MpvRenderWidget.*
│   │   ├── MediaProbe.*
│   │   ├── TrackModel.*
│   │   └── Timecode.*
│   ├── export/
│   │   ├── ExportJob.*
│   │   ├── ExportCommandBuilder.*
│   │   ├── ExportPreset.*
│   │   ├── ExportValidator.*
│   │   └── FfmpegProgressParser.*
│   ├── ui/
│   │   ├── MainWindow.*
│   │   ├── PreviewPanel.*
│   │   ├── RangeTimeline.*
│   │   ├── TransportControls.*
│   │   ├── RangeControls.*
│   │   ├── OutputSettingsPanel.*
│   │   ├── DestinationPanel.*
│   │   └── ExportStatusPanel.*
│   ├── config/
│   │   ├── AppConfig.*
│   │   └── Paths.*
│   └── util/
│       ├── Process.*
│       ├── FileName.*
│       └── Platform.*
└── tests/
    ├── timecode_tests.cpp
    ├── export_command_tests.cpp
    ├── filename_tests.cpp
    └── fixtures/
```

---

## 6. Implementation documents

- [01_PRODUCT_SCOPE_AND_UX.md](01_PRODUCT_SCOPE_AND_UX.md) — exact UI/UX behavior and application states.
- [02_ARCHITECTURE.md](02_ARCHITECTURE.md) — component architecture, mpv bridge, libmpv preview and state model.
- [03_IMPLEMENTATION_PLAN.md](03_IMPLEMENTATION_PLAN.md) — phased build order for a coding agent.
- [04_EXPORT_PIPELINE.md](04_EXPORT_PIPELINE.md) — FFmpeg/ffprobe strategy, format rules and subtitle/audio handling.
- [05_TESTING_ACCEPTANCE.md](05_TESTING_ACCEPTANCE.md) — acceptance criteria and test matrix.
- [06_V2_NOTES.md](06_V2_NOTES.md) — deferred multi-clip design and other post-V1 ideas.

---

## 7. Definition of done for V1

V1 is done when a user can reliably:

1. Open Clipper from a running mpv instance using the hotkey.
2. See the current media in the Clipper preview at the correct initial timestamp.
3. Select one precise clip range using only the mouse if desired.
4. Fine-tune both boundaries frame by frame.
5. Preview only that range with optional looping.
6. Switch subtitle and audio tracks and hear/see the change immediately.
7. Export a valid MP4, MKV, WebM, or GIF using the supported options.
8. See accurate progress and cancel an export.
9. Open or locate the finished file without leaving the editor.
10. Close Clipper without leaving the original mpv session in a broken state.

V2 work must not begin until these V1 acceptance criteria pass.
