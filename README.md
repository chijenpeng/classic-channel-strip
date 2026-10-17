# Classic Channel Strip

Inspired by the SSL 4000 E channel.

Part of the codycodie legacy release collection.

Classic Channel Strip models the 82E242 Rev. 4 filters and four-band EQ with the issue-15 82E10
compressor/limiter. Gate controls remain in saved state for compatibility, but gate attenuation is
permanently bypassed. The circuit-based model uses idealized device equations and documented
approximations; it is not a component-exact recreation of a particular console.

![Classic Channel Strip editor](docs/editor.png)

This release has a new identity (`com.c0dypeng.classicchannelstrip`, `Cdpg/ClCs`) and can coexist
with the private SSL 4000 EQ plug-in. Existing DAW sessions retain their original plug-in identity
and will not be replaced automatically. Parameter IDs, order, ranges, defaults, and the
`SSL4000EQ` state root remain stable; manually transferred state can be compatible where a host
supports that workflow.

## Downloads

Download the latest builds from [GitHub Releases](https://github.com/chijenpeng/classic-channel-strip/releases/latest).
The macOS package includes universal AU and VST3 builds for Apple Silicon and Intel Macs; it is
ad-hoc signed and not notarized. The Windows package includes an unsigned x64 VST3.

## Controls

| Control | Range | What it controls |
|---|---|---|
| Input | −20 … +20 dB | Gain before the card. 0 dBFS represents +18 dBu at the card. |
| HPF, HPF freq | Out / In; 16–390 Hz | 18 dB/oct high-pass from the 82E242 card. The knob is the −3 dB point. |
| LPF, LPF freq | Out / In; 3.2–22 kHz | 12 dB/oct low-pass from the 82E242 card. The knob is the −3 dB point. |
| HF gain, freq, shape | ±18 dB; 1.3–19 kHz; Shelf / Bell | High-frequency shelf or bell. |
| HMF gain, freq, Q | ±18 dB; 640 Hz–9.4 kHz; 0.5–4.1 | High-mid band. |
| LMF gain, freq, Q | ±18 dB; 140 Hz–2 kHz; 0.5–4.2 | Low-mid band. |
| LF gain, freq, shape | ±18 dB; 37–420 Hz; Shelf / Bell | Low-frequency shelf or bell. |
| EQ | Out / In | Switches the four EQ bands; filters remain active. |
| Output | −20 … +20 dB | Gain after the card. |
| Oversampling | 1× / 2× | Processes at host rate or twice that rate. 2× adds 4 samples of latency. |
| Dynamics | Out / In | Stereo-linked 82E10 compressor/limiter. Bypass is bit-transparent. |
| Comp threshold, ratio | +10 … −20 dB; 1:1 … LIMIT | Feed-forward compression with the card's diode-network soft knee. |
| Comp attack, release | Auto / Fast; 0.1 … 4 s | AUTO follows the program-dependent path; FAST selects the short attack branch. |
| Make-up | Off / Auto | Threshold- and ratio-linked automatic make-up. |
| Position | Pre EQ / Post EQ | Places dynamics before or after the filter/EQ card. |
| Sidechain | Input / Filters / EQ / External | Flat key, key filtered by the actual card sections, or the host sidechain input. |

Default order is Input → Dynamics → HPF → LPF → EQ → Output. Post EQ places dynamics after the
card. The FILTERS and EQ sidechain options process a detector copy through the corresponding card
section. External uses the host's optional key input.

## Build

Requires CMake 3.22+, Ninja, and Xcode command-line tools on macOS. The project uses JUCE 8.0.9,
from `third_party/JUCE`, `JUCE_SOURCE_DIR`, or the pinned CMake FetchContent fallback.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --target ClassicChannelStrip_AU ClassicChannelStrip_VST3 classic_channel_test classic_channel_processor_test classic_channel_measure classic_channel_eq_ac classic_channel_snapshot -j2
ctest --test-dir build --output-on-failure
./build/classic_channel_snapshot_artefacts/Release/classic_channel_snapshot docs/editor.png
```

Plug-in copying is disabled by default. Set `-DCOPY_PLUGIN_AFTER_BUILD=ON` only when you want CMake
to copy AU and VST3 bundles to the current user's plug-in folders.

## Tests and tools

| Target / command | Coverage |
|---|---|
| `classic_channel_test` | Offline EQ/filter circuit regression and dynamics checks. |
| `classic_channel_processor_test` | Parameter order, old-state migration, mono/stereo and sidechain buses, permanent gate bypass, pre/post routing, oversampling, latency and audio-thread allocations. |
| `classic_channel_measure` | Magnitude response, sine gain/THD, and realtime measurements. Use `--list` for parameter IDs. |
| `classic_channel_eq_ac` | AC analysis used to derive circuit control laws and trim values. |
| `classic_channel_snapshot docs/editor.png` | Editor snapshot; `--size w h` checks scaling, `--curve` measures response, and `--render in.wav out.wav` renders audio offline. |

## Circuit model and sources

- **82E242 filters and EQ.** The model follows the Rev. 4 card values with its stated trim choices.
  The filter note documents the HPF/LPF transcription and the unresolved C43 68 nF versus 6.8 nF
  marking. The model uses 6.8 nF, supported by the earlier 82E02 topology and resulting response.
  See [the EQ note](docs/research/ssl-eq.md), [filter note](docs/research/82e242-filters.md), and
  [schematic reference](docs/reference/schematic.md).
- **82E10 dynamics.** The issue-15 service drawing supplies the detector, VCA path, control
  networks, timing, stereo link and metering behavior. The AD536J and DBX 2151 are data-sheet
  macromodels because the drawing treats them as IC blocks. See [the dynamics note](docs/research/82e10-dynamics.md).
- **Approximations.** Op-amps use a finite-gain idealized model with soft clipping; switching is
  instantaneous; part tolerances, temperature drift, supply noise and self-noise are not simulated.
  The equations represent the documented circuit, not semiconductor internals.
- **Sources.** Primary schematic archives are linked from the research notes. Raw schematic scans
  and OCR are kept in the private research source and are not included in this public repository.

The four-band EQ matches the analog-recreation project's `ssl_e242` module. Trapezoidal integration
and capacitor pre-warping align circuit corners with knob settings across sample rates. At 44.1 / 48
kHz, one step of the integration rule cannot reproduce the top-octave bell and LPF shape as closely;
2× processing brings them closer to the circuit analysis.

## Real-time behavior

There is no latency at 1× and 4 samples at 2×. Both engines are prepared before audio starts.
Oversampling changes are applied on the message thread after audio leaves the idle engine, so
`processBlock` does not construct an engine. Switching while audio plays restarts the card at the
new rate from rest and can produce a small click.

The detailed circuit model is CPU-demanding. A historical measurement on a busy Apple M3 was
8.5–9.5× real time per channel at 48 kHz / 1×; at 96 kHz it was about 4.6×, and a stereo instance at
48 kHz / 2× used about 45% of one core. Actual use depends on the computer, sample rate and settings.
The processor regression test checks that `processBlock` makes zero allocations with controls still,
moving, oversized blocks, and during an oversampling change.

## Layout

| Path | Contents |
|---|---|
| `Source/dsp/` | The card's circuit model and modified-nodal solver; no JUCE dependency. |
| `Source/PluginProcessor.{h,cpp}` | Processor, stable parameter/state schema, oversampling and meters. |
| `Source/PluginEditor.{h,cpp}` | Scalable editor, real controls and meters, and hover help. |
| `Source/ResponseAnalyser.{h,cpp}` | Offline response measurement for the snapshot utility. |
| `tests/` | Circuit, processor, measurement and snapshot programs. |
| `tools/ssl_eq_ac.cpp` | Source for the `classic_channel_eq_ac` calculation target. |
| `docs/research/` | Maintained circuit research and approximation notes. |
| `Resources/fonts/` | Embedded Inter and JetBrains Mono fonts with SIL Open Font License notices. |

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for JUCE, font and SDK notices.
