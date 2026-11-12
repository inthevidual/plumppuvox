# Plumppu Radiomix – build notes

The third Plumppu tool, and the first that is not a plugin: a small desktop
app that takes a recording with one speaker per channel (a dual-mic
interview, a two-person podcast, the audio of a video) and produces one
automixed, cleaned and levelled mono WAV.

Status: built from these notes on 2026-11-12 (see `README.md`). Kept as the
design record; where the code differs, the code wins (it delivers -14 LUFS
and -1.5 dBTP rather than the -18 LUFS below).

## What the user does

1. Drops a file on the slot (or clicks it to browse).
2. Presses **MIX**.
3. Watches the progress bar; gets `<name>-radiomix.wav` next to the input.

No knobs. Settings are fixed to what worked (below): the final level is
always -18 LUFS in Lagom's LAGOM mode (decided 2026-11-12).

## Interface

One small window, about 480 × 470, not resizable. The file slot, the MIX
key and the progress track share one column (40 px margins).

- **Logo** at the top: `assets/plumppu-radiomix.svg` (copied from
  `~/claude/plumppuvox/plumppu-radiomix.svg`; `radiomix.svg` / `radiomix.png`
  there are variants without "PLUMPPU"). It is traced paths in three colours:
  near-black `#020304` outline, cyan `#05C1F7` offset shadow, white `#FCFCFC`
  letters. Load it with `juce::Drawable::createFromSVG`, no font needed.
- **File slot**: a **black dot-matrix display**: a raised block of glossy
  black glass standing proud of the panel with a rounded, light-catching
  edge (decided 2026-11-12, replacing the recessed version), with white lettering that glows slightly, like an LED sign
  (decided 2026-11-12). Fonts: [Matrix Sans](https://github.com/FriedOrange/MatrixSans)
  v1.810 (OFL-1.1, embed `MatrixSansPrint-Regular.ttf` and
  `MatrixMonoPrintSC-Regular.ttf` plus its `OFL.txt`): the file name on the
  first line in Matrix Sans Print (round separate dots); below it, in Matrix
  Mono Print SC so the digits keep their columns, e.g.
  `VIDEO · 06:26 · 2 CH · 48 KHZ`, and the state at the right (`READY`,
  `MIXING`, `DONE`). Empty: `DROP A FILE HERE` (or click to choose). One
  channel only: `MONO – NOTHING TO MIX` in place of the state. The slot is
  the drop target and opens a file chooser when clicked. Under it, a
  **Dymo label**: a strip of plain white, glossy plastic embossing tape
  stuck on slightly askew (−0.35°), with square-cut ends, standing a little
  proud of the panel so its edges catch the light, a faint ripple along it
  and a narrow gloss highlight along its upper edge; it is kept out of the
  panel's warm grading so it stays pure white. On it, embossed in plain
  black B612 Bold, video first:
  `MP4, MOV, MKV, WEBM – or audio: WAV, AIFF, FLAC, MP3, OGG, M4A`.
  Mockup: `~/claude/plumppu-previews/9-radiomix-dotmatrix.png`.
- **Output selector** (decided 2026-11-12), left of the MIX key in the
  same row (the key moved to the right edge of the column): a 1970s
  three-position **slide switch** set straight into the white panel (no
  faceplate): a long, narrow horizontal slot cut cleanly into the panel,
  showing a dark recess; in it a short
  charcoal plastic tab (26 × 17 px) with a flat, faintly convex face, softly
  rounded corners, fine vertical grip ridges, satin finish with slight
  polishing wear, a highlight along its top edge and a thin shadow at its
  foot. Three stops with firm detents, each with a printed tick and an
  opaque black B612 Bold uppercase legend printed on the panel above the
  slot, on three lines: `REPLACE / SOUND / IN VIDEO`,
  `SAVE / STEREO / TRACK`, `SAVE TWO / SEPARATE / TRACKS`:
  - **REPLACE SOUND** – write a new video with the mix as its soundtrack
    (the video stream copied untouched, metadata and timecode kept). Only
    for video inputs; with an audio input this stop is blocked (its legend
    dimmed) and the switch sits on STEREO TRACK.
  - **STEREO TRACK** – write the mix as a ready-to-use stereo WAV (the mono
    mix on both channels, 48 kHz, 24-bit).
  - **SEPARATE** – write the two processed mic tracks as two separate WAVs
    (each after its own Ren and Lagom pass, before the automix), for mixing
    by hand.
  Clicking a legend or dragging the tab moves it; the tab snaps to the
  nearest stop. Mockup: `~/claude/plumppu-previews/16-radiomix-selector.png`.
- **MIX button** (decided 2026-11-12, replacing the earlier cyan pill): a
  **1970s mechanical transport key** – a chunky cyan (`#1aa9d6`-ish satin
  plastic) block, 1.6 × as wide as tall (144 × 90 px), flat top with rounded
  bevelled edges and a slight overhanging lip, a sliver of thick front
  sidewall visible below the face. It stands in a **well** in the white
  panel itself (no aluminium escutcheon): the opening is about 9 px wider
  than the key all round, its walls roll over at the rim and fall steeply
  ~11 units into a dark housing floor, shaded by the panel's light (shadow
  on the upper-left walls, light catching the lower-right rim), with the
  key's soft shadow cast down into it. Latched, the key visibly sinks into
  the well. Lettering: the play
  triangle and "MIX" in the logo's sticker style (white letters, near-black
  outline, cyan offset shadow; MIX cut from the logo SVG – the M, I and X of
  RADIOMIX with the outline redrawn at the logo's 32.4-unit thickness),
  printed on the face as three thin inks so the key's light falls on it.
  **Latching**: pressing MIX latches the key down (it sinks, the sidewall all
  but disappears) for the whole mix; it pops up when the mix is done or
  cancelled. Light: soft broad sheen on the face towards the lamp, narrow
  glints along the top/left bevels, the sidewall darkening downwards with a
  little bounce light at its foot, a satin micro-texture and faint wear
  scratches, the key's soft shadow and a little ambient occlusion in the
  well; the whole panel
  graded warm in the highlights, slightly cool in the shadows, with a faint
  vignette and grain. Mockup:
  `~/claude/plumppu-previews/12-radiomix-well.png`. Known
  imperfection: the face sheen greys the print a little against the logo.
- **Progress** (decided 2026-11-12, replacing a jewel-lamp row): a
  **raised black-glass status display** below the MIX key (52 px tall, same
  glass as the file display), with the status text on its top line in white
  Matrix Mono Print SC and under it a **continuous turquoise-green
  fluorescent bar** in the logo's turquoise (`#05C1F7`), VFD style: a dim
  phosphor track (the same colour at ~10 %), the lit part bright in the
  middle (`#c4efff` core) and slightly darker at
  its edges, a hotter leading edge, light bleeding into the glass around it
  (two glows, 5 and 14 px), and the fine vertical control grid in front of
  the phosphor (every 3 px). Texts: `PRESS MIX TO START` when idle; while
  mixing the stage and percentage (`READING`, `CLEANING MIC 1 OF 2`,
  `LEVELLING`, `MIXING`, `FINAL LEVEL`, `WRITING` · `58 %`) with `CANCEL`
  at the right (clickable); when done `DONE · SHOW IN FOLDER` (what was
  written follows the output selector). Mockup:
  `~/claude/plumppu-previews/15-radiomix-dymo.png`.
- **All other lettering** (slot text, file types, progress text) dark
  (`#020304`), B612 like the other Plumppu tools.

**Panel** (decided 2026-11-12): moulded **white plastic** (`#f1f0ec`-ish,
like Ren's panel but without the cross), dark text, rendered with the
Plumppu `Surface` like the other two panels, with corner screws; the file
slot and the progress track are recesses in it. The sticker logo with its
black outline stands on the white. Mockup:
`~/claude/plumppu-previews/8-radiomix-mockup-B.png` (the mockup code is a
scratch tool; the panel needs a `PanelStyle::withCross = false` option in the
shared look).

## Languages

English and **Swedish** (decided 2026-11-12). The app follows the system
language (Swedish when it is `sv`, English otherwise). Every string below is
translated; the logo and the MIX key are the same in both. Mockup of the
Swedish panel: `~/claude/plumppu-previews/17-radiomix-svenska.png`. The
fonts (Matrix Sans, B612) both have Å, Ä and Ö.

| Where | English | Svenska |
|---|---|---|
| File display, info line | `VIDEO · 06:26 · 2 CH · 48 KHZ` | `VIDEO · 06:26 · 2 KAN · 48 KHZ` |
| File display, empty | `DROP A FILE HERE` | `SLÄPP EN FIL HÄR` |
| File display, state | `READY` / `MIXING` / `DONE` | `REDO ATT MIXA` / `MIXAR` / `FÄRDIG` |
| File display, mono input | `MONO – NOTHING TO MIX` | `MONO – INGET ATT MIXA` |
| Dymo label | `MP4, MOV, MKV, WEBM – or audio: WAV, AIFF, FLAC, MP3, OGG, M4A` | `MP4, MOV, MKV, WEBM – eller ljud: WAV, AIFF, FLAC, MP3, OGG, M4A` |
| Selector, left | `REPLACE / SOUND / IN VIDEO` | `BYT UT / LJUDET / I VIDEON` |
| Selector, middle | `SAVE / STEREO / TRACK` | `SPARA / STEREO- / SPÅR` |
| Selector, right | `SAVE TWO / SEPARATE / TRACKS` | `SPARA TVÅ / SEPARATA / SPÅR` |
| Status, idle | `PRESS MIX TO START` | `TRYCK PÅ MIX FÖR ATT STARTA` |
| Status, stages | `READING`, `CLEANING MIC 1 OF 2`, `LEVELLING`, `MIXING`, `FINAL LEVEL`, `WRITING` | `LÄSER IN`, `RENSAR MIK 1 AV 2`, `NIVÅJUSTERAR`, `MIXAR`, `SLUTNIVÅ`, `SKRIVER` |
| Status, cancel | `CANCEL` | `AVBRYT` |
| Status, done | `DONE · SHOW IN FOLDER` | `FÄRDIG · VISA I MAPPEN` |
| File chooser title | Choose an audio or video file | Välj en ljud- eller videofil |
| Errors | Can't read this file / No video, can't replace its sound / ffmpeg not found, video files can't be read | Kan inte läsa filen / Ingen video, kan inte byta ut ljudet / ffmpeg saknas, videofiler kan inte läsas |
| Output files | `<name>-radiomix.wav`, `<name>-radiomix.<ext>`, `<name>-radiomix-mic1.wav`, `-mic2.wav` | same file names in both languages |

READY is `REDO ATT MIXA` and DONE is `FÄRDIG`.

## Input and output

- Audio through `juce::AudioFormatManager` (WAV, AIFF, FLAC, OGG, MP3 with
  `JUCE_USE_MP3AUDIOFORMAT=1`).
- Video, and M4A/AAC, through **ffmpeg** run as a separate process:
  `ffmpeg -i <in> -map 0:a:0 -c:a pcm_f32le -f wav -` into a temp file.
  Ship a static LGPL ffmpeg build next to the app (Windows/macOS), use the
  system one on Linux; if none is found, video types are refused with a
  clear message. Licensing: an LGPL build run as a separate program is fine
  next to AGPL code.
- Only the first audio stream. Any sample rate (resample to 48 kHz for the
  Ren engine, as the plugin does).
- Output: mono WAV, 48 kHz, 24-bit, `<input name>-radiomix.wav` in the same
  folder (never overwrite: add `-2`, `-3`).
- **Mix back into the video** (decided 2026-11-12; now the selector's
  REPLACE SOUND position): writes `<input name>-radiomix.<ext>` with ffmpeg: the video stream
  copied untouched (`-c:v copy`), the mix as the only audio stream (AAC
  192 kbit/s for MP4/MOV, the input's codec family otherwise), metadata and
  timecode kept (`-map_metadata 0`, and `-timecode` when the input has one,
  as DaVinci renders do). This is exactly how the test videos were made.

## Processing

Offline, whole file at once, so none of the plugins' real-time compromises
apply (no need to hide latency; lookahead is free).

Per channel (each channel is one mic):

1. **Ren at 80** – the PlumppuVox Ren engine (Rust static library in the
   PlumppuVox repo, `engine/`, C API `plumppuvox_engine.h`), dry mix 0.036
   as at 80, with the breath-aware contrast stage (commit 49ee41f).
2. **Lagom at -18 LUFS, LAGOM mode** – `Rider` from the PlumppuVox repo
   (`lagom/src/Rider.*`), mono, one instance per channel.

Then:

3. **Automix** – a C++ port of `~/cppod/cppod/automix.py` (Dugan-style,
   crosstalk-aware gain sharing), with cppod's defaults from
   `cppod/config.py`: sharpness 2.0, lookahead 0.03 s, attack 0.012 s,
   release 0.35 s, 10 ms gain frames. Steps: frame RMS per mic, 3-frame
   smoothing, 10th-percentile floors; coupling matrix C[i][j] = median of
   E_i/E_j on frames where mic j is dominant and above its 80th percentile,
   clipped to 0..0.7; evidence V = max(E − C·E, 0) + floor; gains
   g = sqrt(V^s / ΣV^s); lookahead max-filter; asymmetric one-pole smoothing;
   total power capped at 1; gains interpolated per sample; peak-safe scale
   to 0.99. N mics works the same way (it is written for N).
4. **Lagom at -18 LUFS, LAGOM** on the mix, for the final loudness and LRA.
5. Peaks end at -1 dBFS (Lagom's limiter).

Special cases:

- **Mono input**: skip automix; Ren then Lagom.
- **Identical channels** (a mono file stored as stereo): detect (correlation
  ≈ 1 over the whole file) and treat as mono, so the gain sharing does not
  run for nothing.
- **Mixed files** (sections of a normal stereo mix, sections of two mics,
  sections of mono – the test video is all three): running automix on the
  whole thing worked acceptably (see results), but the coupling is measured
  over the whole file. If that turns out wrong somewhere, measure coupling
  per section (split where the L/R correlation changes, as in
  the test video at 2:41 and 5:40).

Result on the test video (6.4 min; per-mic Ren 80 + Lagom LAGOM, automix
over the whole file, final Lagom LAGOM):
-17.7 LUFS, LRA 4.6 LU overall; the dual-mic section 2:41–5:40 at
-18.0 LUFS / 3.5 LU; coupling measured -6 / -3 dB; right mic open 94 % of the
time, left 74 %. Things to check by ear before calling it done: whether the
quieter (left) speaker in 2:41–5:40 is pushed back (the mix follows the
right mic more there), and the last 46 s, which come out ~1 dB louder.

## Project

- Sibling folder `~/claude/plumppuradiomix` (this one), same layout and CI
  as the other two (`.github/workflows/build.yml`, Windows / macOS / Linux,
  latency check replaced by an end-to-end render test on a short dual-mic
  clip).
- JUCE GUI app (`juce_add_gui_app`), C++17.
- Reuse, don't copy, the processing: Ren's engine and Lagom's `Rider` both
  live in the PlumppuVox repo (`inthevidual/plumppuvox`); fetch it pinned
  (git submodule or CMake `FetchContent`) – or make Radiomix a third target
  in that repo, which would be simplest. `src/plumppu/` for the `Surface` renderer, the B612
  fonts and the screws (a plastic `PanelStyle` and coat material are new).
- Run the mix on a background thread; the UI only shows progress.
- A command-line mode (`plumppuradiomix in.mp4`) is nearly free and handy for
  batches and for the CI test.

## Decided

- Look: white plastic panel with dark lettering; a raised black-glass dot-matrix file display with a Dymo label under it; a 1970s latching cyan MIX key, with the logo-style play triangle and MIX printed on it, standing in a well in the panel at the right of the row, with a 1970s slide selector for the output (REPLACE SOUND / STEREO TRACK / SEPARATE) set into the panel at its left; a raised black-glass status display with a fluorescent progress bar in the logo's turquoise (above).
- Final level fixed at -18 LUFS, LAGOM.
- Output chosen with a three-position slide selector: REPLACE SOUND, STEREO TRACK, SEPARATE.
- Lagom lives in the PlumppuVox repo (`lagom/`).

## Open questions

1. Radiomix itself: its own repo fetching PlumppuVox, or a third target in
   the PlumppuVox repo? (The latter shares the look, fonts, CI and release
   with no extra plumbing.)
