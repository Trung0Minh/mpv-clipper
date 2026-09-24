# 01 — Product Scope and UI/UX Specification

## 1. Product principle

The tool is a **clipper**, not a general editor.

The UI should remain compact enough to feel like a utility window, but large enough that the preview and timeline are comfortable to use. It should never default to fullscreen.

Primary interaction is **mouse-first**. Keyboard shortcuts may exist as convenience, but no core V1 action may require the keyboard.

---

## 2. Main window layout

Recommended default size:

- approximately 1100×720 on a 1080p desktop
- minimum approximately 900×600
- resizable
- remember the last size/position
- never force maximized/fullscreen

Suggested layout:

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ mpv Clipper                                                     ─  □  × │
├───────────────────────────────────────────────┬──────────────────────────┤
│                                               │ Output                   │
│                                               │                          │
│                VIDEO PREVIEW                  │ Format      MP4 ▼        │
│                                               │ Resolution  Original ▼   │
│                                               │ Quality     High ▼       │
│                                               │ FPS         Original ▼   │
│                                               │                          │
│                                               │ Subtitle                 │
│                                               │ Mode        Hardsub ▼    │
│                                               │ Track       English ▼    │
│                                               │                          │
│                                               │ Audio                    │
│                                               │ Track       Japanese ▼   │
├───────────────────────────────────────────────┴──────────────────────────┤
│  00:00 ┃████████████████████████████████████████████████████┃ 01:48:21 │
│         ▲                        │                          ▲            │
│       Start                   Playhead                    End            │
│                                                                          │
│  [Play/Pause]  [Loop ✓]                  Current 00:12:34.233            │
│                                                                          │
│  Start   [◀ frame] [ 00:12:31.458 ] [frame ▶]                           │
│  End     [◀ frame] [ 00:12:38.792 ] [frame ▶]                           │
├──────────────────────────────────────────────────────────────────────────┤
│ File name   Madoka_00h12m31s-00h12m38s.mp4                              │
│ Save to     /home/user/Videos/Clips                         [Browse...] │
│                                                                          │
│ Status: Ready                                         [ Export Clip ]    │
└──────────────────────────────────────────────────────────────────────────┘
```

The exact visual styling can evolve, but the information hierarchy should remain:

1. preview
2. range timeline
3. range fine-tuning
4. output settings
5. destination/export

---

## 3. Open behavior

### Trigger

Default mpv key binding:

```text
Ctrl+Shift+C
```

The binding must be configurable.

### On hotkey press

The mpv bridge gathers:

- current source path
- current playback timestamp
- current pause state
- selected audio track information
- selected subtitle track information
- optional media title

Then:

1. pause the original mpv instance
2. launch Clipper, or focus the existing Clipper instance
3. pass the launch payload to Clipper

### Initial editor state

After the preview loads:

- selection start = `0`
- selection end = media duration
- selected region = whole timeline
- playhead = mpv timestamp at launch, clamped to range
- preview starts paused
- Loop = ON by default
- currently active mpv audio track should be preselected when possible
- currently active mpv subtitle track should be preselected when possible

Reason for starting paused: it prevents duplicate audio and makes the first interaction deterministic.

---

## 4. Timeline specification

The timeline is the main clipping control.

### Visual elements

It must show:

- full media duration
- Start handle
- End handle
- selected range highlight
- unselected regions visually dimmed
- playhead
- current time
- total duration

### Initial state

```text
Start = 0
End   = duration
```

Therefore 100% of the timeline is highlighted when the window first opens.

### Mouse behavior

#### Click timeline

- seek preview playhead to clicked time
- do not move Start or End markers

#### Drag playhead

- scrub preview
- update current-time label continuously or at a throttled rate

#### Drag Start handle

- change clip start
- Start cannot cross End
- minimum segment duration should be enforced, recommended `>= 1 frame`
- while dragging, preview should seek to the candidate Start position
- playback pauses while a marker is being dragged

#### Drag End handle

Same behavior as Start, mirrored.

#### Handle priority

If Start/End/playhead overlap visually, marker handles must remain easy to grab. Give handles a larger invisible hit box than their visible width.

### Selection highlight

The area `[Start, End]` is the only highlighted segment.

No multi-range UI exists in V1.

---

## 5. Preview playback behavior

### Play

When Play is pressed:

- if playhead is outside `[Start, End]`, seek to Start
- if playhead is at or effectively at End, seek to Start
- play normally
- never intentionally preview beyond End

### Reaching End

If `Loop = ON`:

1. detect playback reaching End
2. seek to Start
3. continue playing

If `Loop = OFF`:

1. stop/pause at End
2. keep playhead at End

Use a small tolerance around End to handle timer/update granularity.

### Pause

Pause at current playhead.

### Seeking while playing

Clicking or dragging the timeline while playing should seek and continue playing unless the user is dragging a boundary marker. Boundary adjustment pauses playback.

---

## 6. Range fine-tuning

Two independent controls:

```text
Start [◀ frame] [HH:MM:SS.mmm] [frame ▶]
End   [◀ frame] [HH:MM:SS.mmm] [frame ▶]
```

### Frame buttons

For Start:

- previous frame moves Start to previous frame boundary
- next frame moves Start to next frame boundary
- preview seeks to the new Start frame

For End:

- previous frame moves End to previous frame boundary
- next frame moves End to next frame boundary
- preview seeks to the new End frame

### Direct time entry

Timestamp fields are editable.

Accepted forms:

```text
SS.mmm
MM:SS.mmm
HH:MM:SS.mmm
```

On commit:

- parse
- clamp to media duration
- reject invalid Start >= End
- update marker
- seek preview to the edited boundary

Display normalized form:

```text
HH:MM:SS.mmm
```

### Variable frame-rate media

Do not calculate frame positions only from nominal FPS. Frame-step should be delegated to libmpv when possible, then read back the actual `time-pos`.

---

## 7. Subtitle controls

### Modes for video outputs

```text
None
Hardsub
Softsub
```

Use the label **Hardsub**, not “Burn-in”, in the main UI.

A tooltip may explain:

> Hardsub permanently renders subtitles into the video image.

### Modes for GIF

```text
None
Hardsub
```

Softsub is not available for GIF.

### Track selector

If mode is Hardsub or Softsub:

- enable subtitle track dropdown
- show human-readable track labels

Recommended label format:

```text
English — ASS — Signs & Songs
English — ASS — Dialogue
Japanese — SRT
External — subtitles.ass
```

Use available metadata:

- language
- title
- codec
- external filename indicator

### Live preview behavior

Changing subtitle track immediately changes the libmpv preview `sid`.

`None` immediately hides subtitles.

For both Hardsub and Softsub modes, preview simply shows the chosen subtitle track. The distinction matters at export time, not preview time.

### Unsupported combinations

If a chosen subtitle codec cannot be exported in Softsub mode for the selected container:

- keep Hardsub available when possible
- disable Softsub
- show an inline explanation

Do not fail only after the user presses Export if incompatibility can be detected earlier.

---

## 8. Audio controls

Audio options:

```text
None
<one audio track>
```

The user cannot export multiple audio tracks in V1.

Recommended labels:

```text
Japanese — AAC 2.0
English — AAC 2.0
Commentary — FLAC 2.0
```

Changing the audio track must immediately update preview playback.

Selecting `None` mutes/disables audio in preview and omits audio from export.

For GIF, the audio section is hidden or disabled because GIF cannot contain audio.

---

## 9. Format controls

Video formats:

```text
MP4
MKV
WebM
```

Image animation format:

```text
GIF
```

Default:

```text
MP4
```

Changing format must immediately update the visible settings and allowed subtitle modes.

---

## 10. Video output settings

No Advanced section in V1.

### Resolution

Options:

```text
Original
1080p
720p
480p
Custom
```

Behavior:

- preserve aspect ratio
- target presets are maximum height
- do not upscale preset outputs beyond source resolution
- ensure encoder-compatible even dimensions when needed

For `Custom`, show width/height inputs with aspect lock ON by default.

### Quality

Options:

```text
High
Medium
Low
```

Default:

```text
High
```

These map internally to encoder-specific presets. Technical CRF/bitrate values are not exposed.

### FPS

Options:

```text
Original
60
30
24
```

Default:

```text
Original
```

Preview does not need to simulate the output FPS.

---

## 11. GIF output settings

When format becomes GIF, replace video settings with:

### Width

Suggested options:

```text
Original
1280
960
720
640
480
Custom
```

Preserve aspect ratio automatically.

Do not upscale unless `Original` naturally uses the original width.

### FPS

Suggested options:

```text
Original
30
24
20
15
12
10
```

Default recommendation:

```text
15
```

### Quality

```text
High
Medium
Low
```

Internally this controls palette generation/dithering strategy and possibly color count.

### Subtitle

```text
None
Hardsub
```

### Audio

Hidden.

---

## 12. Destination controls

### Filename

Auto-generate when:

- source first loads
- Start changes
- End changes
- output format changes

Only auto-update while the user has not manually edited the filename.

Recommended template:

```text
<source_stem>_<start>-<end>.<ext>
```

Example:

```text
Madoka_Movie_00h12m31s-00h12m38s.mp4
```

Milliseconds are omitted from the default filename unless needed to avoid ambiguity.

Sanitize invalid platform filename characters.

### Save directory

Priority:

1. saved user default
2. last used export directory
3. source file directory / `Clips` if writable
4. platform Videos directory

Provide a `Browse...` button.

### Existing file

If target exists, prompt:

```text
File already exists.
[Replace] [Choose New Name] [Cancel]
```

Do not silently overwrite.

---

## 13. Export state UX

### Ready

Export button enabled only if validation passes.

### Exporting

Show:

- progress bar
- percentage
- elapsed status text
- output filename
- Cancel button

Disable settings that would invalidate the running job.

Do not block the Qt UI thread.

### Success

Show:

```text
✓ Export complete
```

Actions:

```text
[Open File] [Open Folder] [Copy Path]
```

Keep the editor open and preserve the current clip/settings.

### Failure

Show concise user-facing error plus expandable technical details.

Example:

```text
Export failed: selected subtitle track cannot be converted to MP4 soft subtitles.

[Show Details]
```

Do not discard current editing state.

---

## 14. Closing behavior

On window close:

- if export is running, ask whether to cancel and close
- release libmpv cleanly
- notify/allow the Lua launcher to finish
- Lua restores original mpv pause state

If original mpv was paused before opening Clipper, leave it paused.
If it was playing, resume it.

---

## 15. Mouse-first requirement

Every V1 operation must be possible with only the mouse:

- open settings dropdowns
- choose tracks
- click timeline
- drag playhead
- drag Start/End handles
- frame-step via buttons
- play/pause
- toggle loop
- choose save directory
- edit filename by clicking field
- export
- cancel export
- open result

Optional keyboard conveniences must never replace visible mouse controls.
