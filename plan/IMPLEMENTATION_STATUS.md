# V1 implementation and verification status

The implementation for phases 0-16 is present. All six automated test suites,
including desktop rendering and IPC, passed on 2026-09-23. Phase 17 is not fully
signed off: the manual acceptance matrix remains open before a V1 release.

## Implemented

| Phases | Implementation |
| --- | --- |
| 0-2 | C++20/Qt/CMake build, launch validation, Lua bridge, private local single-instance IPC, payload consumption |
| 3-4 | Asynchronous ffprobe metadata, embedded libmpv OpenGL rendering, exact seeking, audio/subtitle controls, decoded frame stepping |
| 5-8 | Resizable native editor, draggable timeline, constrained playback/looping, timestamp entry, boundary frame buttons |
| 9-11 | Track matching, internal/external text subtitles, format-specific settings, custom aspect lock, filenames and destination preferences |
| 12-14 | Validated FFmpeg argument arrays, MP4/MKV/WebM/GIF, subtitle rebasing, progress/elapsed time, cancellation and format capability checks |
| 15-16 | Error messages/details, temporary export isolation, overwrite protection, result actions, install rules, desktop entry, CPack archive, CI workflow |

The application owns one source session. Each clip has independent settings;
batch export freezes those settings while the queue runs. See the Multi Clip
implementation section below for the V2 addition.

## Automated verification

Build and checks run in the `mpv-clipper` Conda environment:

- Startup: argument validation, Unicode paths, placeholder-free editor startup,
  timeline mouse events, normal window geometry.
- Export: 22 actual format/subtitle/audio combinations using generated media;
  internal and external text subtitles; audio omission; hardsub frame changes;
  softsub overlap at clip start; playable output and stream mapping.
- Timing/output: short ranges near start/end and between keyframes, comparison
  with accurate source seeks, preset no-upscaling, custom even dimensions/FPS.
- Cancellation: existing destination content survives; private partial files
  are removed. Source overwrite and invalid ranges are rejected.
- Player: decoded seeking/playback, forward/backward frame stepping, variable
  frame cadence, and preview/export audio stream identity. Rendering in this
  test uses libmpv's software render API, not a Qt OpenGL window.
- Bridge: payload content, pause restoration for playing/paused launches,
  duplicate focus requests, failure recovery and temporary-file cleanup.
- IPC: multi-process payload forwarding, acknowledgement, and wait-for-close
  connection lifetime.
- Desktop: real Qt OpenGL preview, frame-step buttons, playback stopping at End,
  format changes, full UI export, Copy Path, and clean window destruction.
  This caught and fixed a context-destruction signal reaching the render widget
  after its derived destructor had run.

The bridge test uses `/usr/bin/mpv`, because conda-forge's mpv executable lacks
Lua support. The application itself links to the Conda libmpv.

## Remaining release verification

Desktop and IPC checks ran successfully with approved access to the display and
local Unix sockets. The complete suite passed with no skipped tests (21.40 s).

On a desktop, run the full test suite from the activated environment:

```sh
ctest --test-dir build --output-on-failure
```

With Xvfb installed, the CI workflow runs the same suite under a software OpenGL
display. The workflow has been added but has not been run on GitHub in this session.

The empty editor's offscreen layout was rendered and inspected. Physical audio,
real mpv hotkey/window focus behavior, high-DPI/display changes, ASS font fidelity,
and representative-file manual checks in `05_TESTING_ACCEPTANCE.md` remain open.

## Intentional implementation details

- Bridge invocations pass `--wait-for-close`. A secondary launcher keeps its IPC
  connection alive until the primary exits, so another mpv session does not resume
  prematurely. Ordinary secondary invocations still exit after acknowledgement.
- FFmpeg exports into a private file in the destination folder. Successful output
  replaces an approved existing file only if its size/modification time still
  match. Failed/canceled exports never truncate the existing destination.
- Hardsub sources use safe temporary symlink names instead of interpolating source
  paths into FFmpeg's filter grammar. Text Softsub events are converted to ASS,
  clipped/rebased, then encoded for the target container.
- Animated ASS/karaoke effects that overlap the clip start can change meaning in
  rebased Softsub. Hardsub preserves the original visual timing. Bitmap subtitle
  export is intentionally disabled, as permitted by the V1 plan.
- Late exports now seek near Start and retain source-relative timestamps through
  exact video/audio trimming; early clips keep the original decode path.
- The Linux tar archive requires installed dependencies; it is not a standalone
  AppImage. The Conda build depends on its environment. No license was invented.

Do not mark the original acceptance checklist complete until the remaining
manual tests pass.

## Multi Clip implementation

Multi Clip mode now supports independent range/output/track settings, clip
selection and deletion, overlapping timeline ranges, separate-file batch export,
per-clip queue states/progress, cancel current/all, retry failed, and opening a
completed output. A new offscreen integration check verifies real MP4/GIF batch
outputs in different folders, independent settings, cancellation, retry, and deletion.
Existing startup/export/player/bridge checks also passed after the implementation.
The desktop smoke test now includes a mixed-format batch, but its rerun was blocked
by the approval service reporting missing provider credentials. Desktop/manual
acceptance of Multi Clip remains pending; this is not a release sign-off.

## Interaction optimization

Dragging updates the visible selection immediately while preview seek requests are
coalesced at 70 ms intervals and use keyframes. Release, timestamp entry, and frame
stepping retain exact seeks and cancel any pending approximate seek. Property
writes to libmpv are asynchronous. Clip settings/filenames are committed on release;
queue rows update in place instead of resetting the entire list.

The reproducible offscreen benchmark (`MPV_CLIPPER_BENCHMARK=1 batch_tests
-platform offscreen`) measured 200 drag updates with 100 clips at 299 ms / 201 list
resets before and 4 ms / zero resets after. This measures UI work, not large-video
decoding. Startup, batch, player, export and bridge suites passed. The player test
also checks that a pending scrub cannot override the exact release position.
Desktop verification remained blocked by the approval service's missing credentials.

## Fast export seeking

Late exports now use an input seek to a whole second at least two seconds before
Start, with copyts/start_at_zero preserving the original source-relative timeline.
The trim/atrim filters still determine the exact output boundaries. Video, audio,
subtitle and encoder quality settings are unchanged.

The seek export regression compares against the former full-decode command using
60-second HEVC 10-bit media with 10-second GOPs. It covers MP4/MKV/WebM/GIF,
non-keyframe cuts, internal/external animated ASS, overlapping softsubs, real
ExportJob subtitle preparation, nonzero container start times, and variable frame
cadence. Decoded video frames and packet timestamps/durations match; decoded AAC
is checked within one sample of phase and 1% normalized signal error (decoder noise
and resampling can differ after a seek). Example measurements: MP4 144 ms vs
269 ms, internal hardsub 187 ms vs 316 ms. These are small synthetic measurements,
not guarantees for every source or machine.

## Marker selection and gentler zoom

Wheel zoom now uses a 1.1 factor per notch instead of 1.5. Clicking a range
endpoint selects it with a white outline; both frame buttons adjust that endpoint.
Clicking elsewhere on the detailed timeline clears the selection so the buttons
step playback without changing the clip range. Clicking within a marker's hit
area preserves its timestamp until dragged. Changing clips clears selection.

Startup timeline interaction, decoded player stepping, and batch checks pass.
The desktop smoke test covers both directions for Start, End, and playback;
it requires a graphical environment and was not run for this change.
