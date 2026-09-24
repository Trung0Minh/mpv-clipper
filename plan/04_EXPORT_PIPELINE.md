# 04 — Export Pipeline and FFmpeg Rules

## 1. Principles

1. Preview is handled by libmpv.
2. Final output is handled by FFmpeg.
3. V1 always prioritizes correctness and predictable compatibility over maximum transcoding speed.
4. The GUI exposes only simple presets; codec-specific details remain internal.
5. Argument arrays are constructed directly. Never run FFmpeg through a shell command string.

---

## 2. Clip timing

Inputs:

```text
startSec
endSec
clipDuration = endSec - startSec
```

Favor accurate seeking over keyframe-only stream-copy behavior.

V1 should re-encode video rather than promise frame-accurate stream-copy clipping.

Recommended timing structure:

```text
ffmpeg -i INPUT -ss START -t DURATION ... OUTPUT
```

or another validated ordering chosen after timing tests.

The implementation must have automated/manual tests around boundary accuracy. Do not assume all `-ss` placements behave identically with subtitle filters and every demuxer.

Do not expose stream-copy mode in V1.

---

## 3. Common video mapping

Always explicitly map the intended streams.

Conceptually:

```text
-map 0:<video stream>
-map 0:<selected audio stream>   # if audio enabled
-map 0:<selected subtitle>       # only for softsub
```

For Hardsub, the subtitle is rendered into video and is not separately mapped as a subtitle track unless future behavior explicitly adds that option.

---

## 4. Resolution scaling

### Original

No scale filter unless needed to satisfy encoder pixel-dimension requirements.

### Presets

Use target maximum height:

```text
1080p
720p
480p
```

Preserve aspect ratio and avoid upscaling.

Conceptual scale expression:

```text
scale=-2:min(TARGET_H,ih)
```

But validate expression behavior and SAR handling.

### Custom

Aspect lock on by default.
Ensure even dimensions where encoder requires them.

---

## 5. FPS

### Original

Do not apply FPS conversion.

### 60 / 30 / 24

Apply explicit FPS filter/output rate.

The UI does not promise motion interpolation. Higher FPS than source may duplicate frames.

Do not silently enable optical flow/interpolation.

---

## 6. Quality presets

The user sees:

```text
High
Medium
Low
```

Suggested internal defaults can start as follows and be tuned after real-world testing.

### H.264 / libx264

```text
High    CRF 18, preset medium
Medium  CRF 23, preset medium
Low     CRF 28, preset medium
```

### VP9 / libvpx-vp9

Use constant-quality mode with internal values selected after testing, for example:

```text
High    lower CQ/CRF
Medium  medium CQ/CRF
Low     higher CQ/CRF
```

Do not surface these numbers in the normal UI.

### Audio

Suggested defaults:

MP4/MKV AAC:

```text
192k stereo-ish source
preserve channel count when reasonable
```

WebM Opus:

```text
160k–192k typical
```

Exact mapping should adapt sensibly to channel count.

---

## 7. MP4 preset

### Video

```text
codec: libx264
pixel format: yuv420p for broad compatibility
```

### Audio

```text
codec: AAC
```

### Softsub

MP4 commonly expects `mov_text` for text subtitles.

Allow Softsub only when the selected source subtitle is text-based and FFmpeg can convert it to `mov_text`.

Common supported source candidates:

- SRT/SubRip
- ASS/SSA, with style loss expected when converted
- WebVTT, when conversion succeeds

Image-based subtitle streams should not advertise MP4 Softsub in V1.

If ASS/SSA -> mov_text, warn in tooltip/inline info that styling is not preserved.

### Hardsub

Render selected subtitle into video. This is preferred when preserving ASS styling matters.

---

## 8. MKV preset

### Video

Use H.264/libx264 in V1 for simplicity/predictability.

### Audio

Use AAC by default, or another deliberately selected universal preset.

### Softsub

MKV is the most permissive supported container.

Prefer subtitle stream copy when safe and when timing remains correct after clipping. If stream-copy causes timestamp/timing issues, re-encode text subtitles to a compatible subtitle codec.

Text ASS/SRT should be treated as first-class V1 workflows.

Image subtitle softsub support can be added if testing proves reliable, but it is not required for initial V1 acceptance.

### Hardsub

Same render-to-video path as other formats.

---

## 9. WebM preset

### Video

```text
VP9
```

Recommended encoder:

```text
libvpx-vp9
```

### Audio

```text
Opus
```

### Softsub

WebM subtitle support should be limited to WebVTT-compatible text.

If selected subtitle cannot be converted safely to WebVTT:

- disable Softsub
- leave Hardsub available if supported

---

## 10. GIF pipeline

GIF has:

- no audio
- no Softsub

Use a palette pipeline for acceptable quality.

Conceptual filter graph:

```text
[video]
  optional subtitles
  -> fps
  -> scale
  -> split
      -> palettegen
      -> paletteuse
```

A common FFmpeg shape:

```text
-filter_complex "...
split[s0][s1];
[s0]palettegen=... [p];
[s1][p]paletteuse=..."
```

### Width

Preserve aspect ratio.

### GIF quality mapping

Suggested concept:

```text
High    256 colors, higher quality dithering
Medium  ~192 colors, balanced dithering
Low     ~128 colors, lighter output
```

Tune based on visual tests and output sizes.

---

## 11. Hardsub rendering

### Common text subtitle targets

V1 should explicitly support common anime/subtitle workflows:

- ASS/SSA
- SRT
- WebVTT when supported by FFmpeg/libass path
- external `.ass` / `.srt`

The system should verify FFmpeg has subtitle/libass support.

### Internal subtitle stream

Use the selected subtitle stream/index in the subtitles filter or an equivalent robust FFmpeg filter graph.

### External subtitle

Use the external subtitle filename/path.

All paths must be escaped for FFmpeg filter syntax correctly. Do not reuse shell escaping rules for filter escaping.

### Fonts

ASS hardsub quality may depend on fonts available to libass.

V1 behavior:

- use fonts available on the system
- use Matroska font attachments if FFmpeg/libass handling is validated
- if fonts are missing, export may render fallback fonts

A future V2 enhancement can add explicit font-directory selection/diagnostics.

---

## 12. Softsub timing

Clipping a source with a subtitle track must produce subtitle timestamps aligned to the new clip start.

This must be explicitly tested.

Test cases:

- subtitle begins before clip Start and overlaps Start
- subtitle begins exactly at Start
- subtitle begins midway through clip
- subtitle extends beyond End
- ASS karaoke/effects around Start

Do not assume correct timestamp rebasing without validation.

---

## 13. Track mapping

ffprobe stream indexes are the export authority.

The GUI track object should retain:

```text
ffprobe stream index
codec
language
title
disposition
external path if applicable
```

When selecting a track in the UI, export maps that exact stream.

---

## 14. External subtitle tracks

mpv may load an external subtitle that does not exist as a stream inside the video file.

The launch payload and embedded libmpv track list may reveal this.

For V1:

- preview should support it if libmpv supports it
- Hardsub should support it for common text formats
- Softsub should include the external file as an additional FFmpeg input when container-compatible

Example conceptual mapping:

```text
ffmpeg -i video.mkv -i subtitles.ass ...
```

Track mapping must distinguish media input index 0 from subtitle input index 1.

---

## 15. Export progress

Start FFmpeg with machine-readable progress, e.g.:

```text
-progress pipe:1
-nostats
```

Parse fields such as:

```text
out_time_us
out_time_ms
progress
```

Calculate:

```text
percent = exportedTime / clipDuration
```

Clamp to 0–100.

Never parse the human-readable moving stderr status line as the primary progress mechanism.

---

## 16. Partial output handling

Write directly to the requested final path only if cleanup is guaranteed.

Safer option:

```text
<filename>.partial.<ext>
```

Then atomically rename on success when filesystem permits.

Benefits:

- user never mistakes partial file for completed export
- cancellation cleanup is simpler

On success:

```text
partial -> final
```

On cancel/failure:

```text
delete partial
```

---

## 17. Dependency capability probing

At startup or first use, inspect FFmpeg capabilities.

Required or preferred:

```text
encoder libx264
encoder aac
encoder libvpx-vp9
encoder libopus
filter subtitles
filter palettegen
filter paletteuse
```

If VP9 unavailable:

- disable WebM

If subtitles filter unavailable:

- disable Hardsub

If palette filters unavailable:

- disable GIF export

Show a concise reason in the UI.

---

## 18. Security and robustness

- never shell-concatenate filenames
- validate output extension
- sanitize user-created filename
- do not allow output path equal to source path
- use explicit stream mapping
- bound captured stderr log size
- clean temporary files
- correctly handle Unicode paths
