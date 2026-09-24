# 05 — Testing and Acceptance Criteria

## 1. Core UX acceptance

### Launch

- [ ] Hotkey opens Clipper from mpv.
- [ ] Original mpv pauses.
- [ ] Clipper preview loads the same source.
- [ ] Initial playhead matches the mpv timestamp within reasonable seek tolerance.
- [ ] Closing Clipper restores original mpv pause/play state.

### Initial range

- [ ] Start = 0.
- [ ] End = media duration.
- [ ] Full timeline appears selected/highlighted.

### Timeline

- [ ] Start marker can be dragged with mouse.
- [ ] End marker can be dragged with mouse.
- [ ] Markers cannot cross.
- [ ] Click-to-seek works.
- [ ] Playhead drag/scrub works.
- [ ] Selection highlight always matches Start/End.

### Preview

- [ ] Play previews only selected range.
- [ ] Loop ON repeats selected range.
- [ ] Loop OFF stops at End.
- [ ] Adjusting markers pauses appropriately.

### Fine tuning

- [ ] Start previous/next frame works.
- [ ] End previous/next frame works.
- [ ] Timestamp text entry works.
- [ ] Invalid timestamps are rejected clearly.

---

## 2. Track acceptance

### Audio

Test files with:

- one audio track
- two audio tracks
- multiple languages
- no audio

Acceptance:

- [ ] dropdown lists correct tracks
- [ ] switching updates preview immediately
- [ ] None mutes preview and removes audio from export

### Subtitle

Test:

- internal ASS
- internal SRT
- external ASS
- external SRT
- no subtitles
- multiple subtitle tracks

Acceptance:

- [ ] selected subtitle updates preview immediately
- [ ] None hides subtitles
- [ ] Hardsub exports visible subtitle
- [ ] Softsub exports selectable subtitle where supported
- [ ] unsupported Softsub combinations are disabled before export

---

## 3. Export format matrix

Minimum matrix:

| Format | Subtitle | Audio | Result |
|---|---|---|---|
| MP4 | None | track | valid |
| MP4 | Hardsub ASS | track | valid |
| MP4 | Softsub text | track | valid/selectable |
| MP4 | None | None | valid silent clip |
| MKV | None | track | valid |
| MKV | Hardsub ASS | track | valid |
| MKV | Softsub ASS/SRT | track | valid/selectable |
| WebM | None | Opus output | valid |
| WebM | Hardsub | track | valid |
| WebM | compatible Softsub | track | valid/selectable |
| GIF | None | n/a | valid animation |
| GIF | Hardsub | n/a | valid animation |

---

## 4. Timing accuracy tests

Use sources with visible frame numbers/time burn-ins if possible.

Test ranges:

- start/end on keyframes
- start/end between keyframes
- 1 second clip
- sub-second clip
- 10 second clip
- clip near file start
- clip near file end

Acceptance:

- first exported visual frame matches selected Start to expected decode tolerance
- last exported frame does not extend materially beyond selected End
- audio begins aligned with video
- subtitles align with video after clip time rebasing

---

## 5. Resolution/FPS tests

### Resolution

- [ ] Original preserves dimensions.
- [ ] 1080p never upscales a 720p source.
- [ ] 720p downscales 1080p correctly.
- [ ] 480p works.
- [ ] Custom aspect lock works.
- [ ] odd source sizes result in encoder-valid output sizes.

### FPS

- [ ] Original leaves source cadence unconstrained.
- [ ] 60 works.
- [ ] 30 works.
- [ ] 24 works.

No motion interpolation is expected.

---

## 6. GIF tests

- [ ] Width presets preserve aspect ratio.
- [ ] GIF has no audio.
- [ ] Softsub option is absent.
- [ ] Hardsub appears visually when selected.
- [ ] palette pipeline produces acceptable quality.
- [ ] High/Medium/Low produce meaningfully different size/quality tradeoffs.

---

## 7. Filename/path tests

Test paths containing:

- spaces
- Unicode/Vietnamese/Japanese characters
- brackets
- apostrophes
- long directory names

Acceptance:

- [ ] preview loads
- [ ] ffprobe works
- [ ] export works
- [ ] Open File/Open Folder works

Test filename behaviors:

- [ ] auto-name updates with range
- [ ] manual name stops automatic renaming
- [ ] format change updates extension
- [ ] existing file prompts
- [ ] invalid filename characters are sanitized

---

## 8. Failure tests

- [ ] FFmpeg missing.
- [ ] ffprobe missing.
- [ ] libmpv unavailable.
- [ ] source deleted after opening.
- [ ] output directory becomes read-only.
- [ ] invalid/corrupt media.
- [ ] export process returns nonzero.
- [ ] user cancels export.

Acceptance:

- app stays responsive
- user gets a clear message
- current clip selection/settings remain intact where possible
- partial outputs are cleaned up

---

## 9. Performance checks

- UI remains responsive during export.
- Timeline dragging feels immediate.
- Subtitle/audio switching occurs without restarting the whole application.
- Preview does not perform output-quality re-encoding.
- Long videos do not cause timeline UI degradation simply because duration is long.

---

## 10. V1 final acceptance checklist

V1 can be released only when all are true:

- [ ] mouse-only clipping workflow is complete
- [ ] single-range selection is stable
- [ ] live audio/subtitle preview is stable
- [ ] frame-step boundaries are usable
- [ ] MP4 works
- [ ] MKV works
- [ ] WebM works when dependencies are available
- [ ] GIF works
- [ ] export progress/cancel works
- [ ] result actions work
- [ ] original mpv state restores correctly
- [ ] no known data-loss/overwrite bug
- [ ] no P0/P1 issue remains

Only after this checklist passes should V2 features begin.
