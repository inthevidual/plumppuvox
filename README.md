# PlumppuVox Ren

A one-knob voice isolation plugin (VST3). Its sibling, the volume rider
[PlumppuVox Lagom](lagom/README.md), lives in this repository too and is
built and released alongside it, and so is
[Plumppu Radiomix](radiomix/README.md), a desktop app that automixes
recordings with one speaker per channel using both plugins' processing.

Turn the dial from 0 to 100 to remove
background noise, breaths, clicks and room sound from speech, in real time.

<img src="docs/screenshot.png" width="420" alt="PlumppuVox Ren interface">

- **0** leaves the audio untouched, **100** keeps only the voice. **80**, the
  default, is the everyday setting.
- Runs on Windows (x64), macOS (Apple Silicon) and Linux.
- Mono and stereo, any sample rate (the model runs at 48 kHz; other rates are
  converted internally).
- About 4 % of one CPU core for mono and 6 % for stereo at 48 kHz on a
  current desktop CPU.
- Latency is 40 ms (1920 samples at 48 kHz) and is reported to the host, so
  editors and DAWs compensate for it automatically.

The sound is tuned to match an earlier one-knob tool, measured from its renders
at every tenth setting. Each block of audio passes through four stages:

1. **Level normalisation**, so quiet recordings are treated as well as loud
   ones. The gain is undone exactly at the output.
2. **[DeepFilterNet3](https://github.com/Rikorose/DeepFilterNet)**, a neural
   speech enhancement model, at full strength. The model is compiled into the
   plugin, so there are no extra files to install.
3. **Contrast**: wherever the model has turned something down by more than
   6 dB (a breath, a click, noise between words) it is pushed down further,
   up to 20 dB more, while speech the model only nibbled at is eased back.
4. **The dial** mixes the result with the untouched input along the original
   tool's curve, so each setting sounds like the same setting did there.

Noise removal is its strength. Reverb is reduced only as far as the model does
it, so a very roomy recording will not become dry.

## Install

Download the build for your system from the GitHub Actions run (or build it
yourself, see below) and copy `PlumppuVox.vst3` to:

| System  | Folder                                    |
|---------|-------------------------------------------|
| Windows | `C:\Program Files\Common Files\VST3`      |
| macOS   | `~/Library/Audio/Plug-Ins/VST3`           |
| Linux   | `~/.vst3`                                 |

The macOS build is not notarized. If macOS refuses to load a downloaded copy, run:

    xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/PlumppuVox.vst3

## Build

Requirements: CMake 3.22+, a C++17 compiler, and [Rust](https://rustup.rs).
JUCE is downloaded automatically.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The plugin ends up in `build/PlumppuVox_artefacts/Release/VST3/` (and Lagom in
`build/PlumppuLagom_artefacts/Release/VST3/`). Add
`-DPLUMPPUVOX_COPY_AFTER_BUILD=ON` to install them into your plugin folder.

`-DPLUMPPUVOX_BUILD_TOOLS=ON` also builds `plumppuvox_tool` (and Lagom's
`plumppulagom_tool`); `plumppuvox_tool` renders a WAV
file through the plugin offline, checks the reported latency and writes a
screenshot of the interface.

## Layout

- `engine/` – Rust static library: DeepFilterNet plus the level and contrast stages, behind a small C API
- `src/` – the JUCE plugin (processing, sample rate conversion, interface)
- `src/plumppu/` – the shared Plumppu look (metal panel, flag cross, knob,
  lamps, logo, lever switch, fonts), used by both plugins
- `lagom/` – PlumppuVox Lagom, the volume rider
- `radiomix/` – Plumppu Radiomix, the automix app
- `assets/fonts/` – the B612 typeface used for labels and readouts
- `patches/` – a small JUCE patch that works around a VST3 bug in Audacity on Linux
- `tools/` – development tool

## License

PlumppuVox is released under the GNU AGPL v3 (see `LICENSE`), as required by
JUCE. DeepFilterNet is MIT / Apache-2.0 licensed. The B612 and Barlow fonts are
under the SIL Open Font License (`assets/fonts/OFL.txt`, `assets/fonts/OFL-Barlow.txt`),
as is Matrix Sans, used by Radiomix (`assets/fonts/OFL-MatrixSans.txt`).
