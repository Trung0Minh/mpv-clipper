# 06 — V2 Notes — Deferred Until V1 Is Complete

This file is intentionally separate so V2 ideas do not leak into V1 implementation scope.

The first V2 priority should be **multi-clip mode**.

Multi Clip is the current Single Clip workflow repeated independently for multiple
clips from the same source, with one action to export them all. Each clip owns its
range and complete settings; there are no shared output settings.

---

## 1. Single Clip / Multi Clip mode selector

Add an explicit mode selection near the top of the editor:

```text
Mode:  [ Single Clip ] [ Multi Clip ]
```

Default remains Single Clip.

The user must choose the conceptual mode before multi-range editing becomes available because timeline behavior, export naming, and setting scope differ.

Do not silently convert a single-range session into multi-range behavior.

---

## 2. Multi Clip timeline model

Instead of one `[Start, End]`, support a list:

```text
Clip 1: start/end
Clip 2: start/end
Clip 3: start/end
...
```

Timeline should render each selected segment as a separate highlighted block.

Possible interactions:

- `Add Clip` creates a new range around current playhead or current selection
- click a highlighted range to select/edit it
- drag the active range's Start/End handles
- delete selected range
- reorder clip list if useful

Allow overlapping or identical ranges: independent clips may export the same
footage with different settings. Provide clip-list selection so overlapping
ranges remain individually selectable.

---

## 3. Multi Clip preview behavior

Two possible preview modes:

### Active Clip

Play/Loop only the currently selected clip range. Selecting a clip restores its
own timestamps, loop preference, audio/subtitle selection, output controls,
filename, and destination. Preview uses that clip's audio/subtitle settings; output
resolution, FPS, and encoding quality remain export-only, as in V1.

### Sequence Preview

Play selected clips consecutively, jumping across unselected gaps.

Start V2 with Active Clip preview. Sequence Preview can be a second step.

---

## 4. Settings scope in Multi Clip mode

Each clip is a complete, independent Single Clip configuration from the first
multi-clip iteration. Per-clip settings are core functionality, not later overrides.

Each clip owns:

- Start/End range and loop preference
- format (MP4, MKV, WebM, or GIF)
- resolution, custom dimensions, and aspect lock
- quality and FPS
- GIF width, FPS, and quality
- audio track or None
- subtitle mode and track, including external subtitle selection
- output filename and automatic/manual naming state
- save directory

The existing editing controls operate on the selected clip only. Switching clips
preserves all edits and restores the newly selected clip's configuration. Editing,
deleting, or exporting one clip must not change another clip's configuration.

New clips may start with a one-time copy of the current settings for convenience,
but thereafter all values are independent. Application defaults only initialize
new clips; changing a default must not alter existing clips. No implicit global
settings or "apply to all" behavior is included.

---

## 5. Multi Clip export modes

Potential modes:

### Export Separate Files

```text
source_clip_001.mp4
source_clip_002.webm
source_clip_003.gif
```

This is the first V2 implementation: **Export All** queues every clip using its
own settings, filename, and destination. Mixed formats and output directories
are supported in one batch. Numbered names are only defaults; each clip retains
V1 automatic naming and manual filename editing. Validate every clip and resolve
destination collisions before starting, including collisions within the batch.

### Join Clips

Optional later mode:

- concatenate selected ranges into one output
- no transitions initially

Joining creates additional complexity around timestamps, subtitles and codec parameters, so keep it after separate-file batch export.

---

## 6. Export queue

Multi Clip requires a job queue. Process clips sequentially using independent,
immutable snapshots of their configurations. A failed or cancelled clip must
not discard other clips or their completed outputs. Cancel current advances to
the next pending clip; Cancel all stops the active job and cancels pending jobs.
Retry failed uses the failed clip's own configuration. Preserve V1 overwrite
confirmation and partial-file cleanup for each output.

Needed states:

```text
Pending
Exporting
Done
Failed
Cancelled
```

UI can show:

```text
1  00:12:31–00:12:38  Done
2  00:20:10–00:20:18  Exporting 64%
3  00:45:00–00:45:04  Pending
```

Support:

- cancel current export
- cancel all
- retry failed
- open completed file

---

## 7. Potential V2+ enhancements

Only consider after multi-clip is stable.

### Timeline improvements

- frame thumbnails
- zoomable timeline
- mouse-wheel zoom
- mini overview + detailed timeline
- audio waveform
- marker snapping

### Export improvements

- MOV option if there is demand
- hardware encoding presets
- stream-copy fast mode with clear keyframe limitations
- custom filename templates
- export presets

### Subtitle improvements

- font availability diagnostics
- custom fonts directory
- subtitle styling preview controls
- dual subtitles

### Editing improvements

- crop tool
- rotate/flip
- simple scale presets for social media
- small fade-in/fade-out

### Integration improvements

- optional “send current selection back to mpv”
- recent clips history
- remember per-series export directory
- drag-and-drop source directly into Clipper

---

## 8. V2 guardrail

Do not begin the multi-clip timeline, queue, thumbnails, waveform, crop, or codec-advanced work while V1 still has unresolved core issues.

V1 must first be reliable as a focused single-clip tool.
