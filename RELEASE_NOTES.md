# Classic Channel Strip release notes

## v0.2.0

Classic Channel Strip now runs the 82E242 card as a linear discrete-time state space extracted from the same schematic netlist, 24 states in all. Version 0.1.0 solved the whole netlist every sample with nonlinear op-amp devices.

- **About 100x less CPU.** Stereo, 48 kHz, 512-sample blocks on an Apple M3: the defaults run at about 260–270x real time at 2x (about 825x at 1x); with both filters, every band and the dynamics in, about 270x at 1x and 120x at 2x. An offline plug-in host benchmark at the native defaults (stereo, 48 kHz, 512-sample blocks, 2x): 268-270x, 40 µs per block. v0.1.0 ran at roughly 2-4x in the same settings.
- **Same sound below the card's op-amp limit.** The response matches v0.1.0 within 0.05 dB from 20 Hz to 20 kHz over 61 settings at 48 and 96 kHz.
- **Clipping is the one difference.** The op-amp curve (linear to ±14.85 V, +4.6 dBFS at the card with the EQ flat) is now applied at the section outputs instead of inside every op-amp's feedback loop. The two versions differ only when the card is driven past that limit, where the new engine overstates the distortion of a narrow, boosted band.
- **New CARD overload lamp** in the editor: it lights when an op-amp in the card passes its linear limit, holds for about a second and reads the peak, in dBFS at the card.
- Parameters, plug-in identity, saved state and the 2x Oversampling default are unchanged: saved sessions load unchanged.
- Documentation: the circuit notes describe the state-space engine, its approximations and a headroom map showing which op-amp clips first.

Packages:

- macOS Universal: AU and VST3, arm64 + x86_64; ad-hoc signed, not notarized.
- Windows x64: VST3, unsigned; requires the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

Validation (macOS, Apple Silicon): CTest 2/2 passed; both architecture slices verified; `auval -v aufx ClCs Cdpg` passed; the VST3 loads in an offline host with the same parameters in the same order as v0.1.0; the editor was inspected at minimum, default and maximum sizes and with the CARD lamp lit. Each ZIP includes installation instructions, dependency notices and build provenance, with a SHA-256 checksum companion. No DAW-host test on an end-user Windows system or Intel Mac runtime test has been performed.

## v0.1.0

Inspired by the SSL 4000 E channel: 82E242 Rev. 4 filters/four-band EQ and the issue-15 82E10 compressor/limiter.

Classic Channel Strip is part of the codycodie plug-in collection. The circuit engine has been refactored to remove unused device models and unrelated amplifier builders while preserving its modeled behavior. The gate remains permanently bypassed; its saved parameters remain for compatibility.

- macOS Universal: AU and VST3, arm64 + x86_64.
- Windows x64: VST3; requires the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
- macOS bundles are ad-hoc signed, not notarized. Windows builds are unsigned.
- Native Apple Silicon CTest passed 2/2, including old-state migration, parameter order, routing, sidechains, oversampling/latency and allocation checks. Both architecture slices were verified; the editor was inspected at minimum/default/maximum sizes.
- Native Windows x64 build and CTest passed 2/2; package architecture, VST3 factory and cross-platform class IDs were verified.

This release has an independent plug-in identity. Existing SSL 4000 EQ instances stay attached to the original product; the parameter IDs and `SSL4000EQ` state schema are preserved for compatible manual state transfer.

The detailed circuit model is CPU-demanding. It uses documented approximations and IC macromodels rather than claiming a component-exact recreation of a particular console.

Each ZIP includes installation instructions, dependency license notices and build provenance; SHA-256 checksum companions are supplied. No DAW loading test on an end-user Windows system or Intel Mac runtime test has been performed.

