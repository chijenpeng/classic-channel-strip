#pragma once

#include "Module.h"

#include <array>
#include <vector>

namespace ar::fx
{
/** The SSL 4000 E channel EQ card 82E242 ("black knob"), the whole card, mono, solved from its
    schematic with the circuit engine: the HPF (T11 / T12, 18 dB/oct) and LPF (T13, 12 dB/oct),
    then the four bands (T1-T10), each section behind the card's own in / out switch. See
    docs/research/ssl-eq.md and docs/research/82e242-filters.md.

    Level: 1.0 full scale = 0 dBFS = +18 dBu = 8.70 V peak at the card. The op-amps are linear to
    +4.6 dBFS (flat) and saturate softly above. No latency.

    Threads: prepare() builds the circuit for a rate and finds its operating point (allocates;
    not for the audio thread), and so does reset(). setParam() and process() are for the audio
    thread: process() changes the circuit at the control rate (every 32 samples) while a control
    moves or a switch flips, refactoring the network that changed. */
ModulePtr makeSslE242();

/** Parts of the card, for tests and tools: eqOnly is the four bands alone, built exactly as the
    analog-recreation module that this one extends (12 controls, no filter network, no EQ IN
    switch); filtersOnly is the HPF / LPF section alone (4 controls). */
enum class SslSections { all, eqOnly, filtersOnly };

/** The large coupling capacitors that the card model replaces by shorts, except the ones in
    kSslKeptCapacitors. Names are the schematic's reference designators. */
inline constexpr std::array<const char*, 15> kSslLargeCapacitors {
    "C22", "C1", "C2", "C6", "C7", "C11", "C14", "C20", "C21", "C51", "C39", "C41", "C42", "C46", "C47" };
// (C39 stands for C39 and C40, in series in the HPF's output network.)

/** The ones the model keeps as capacitors: shorting any of them moves the response by more than
    0.05 dB somewhere in 20 Hz - 20 kHz (README, "Approximations"). Bit i is kSslLargeCapacitors[i]. */
inline constexpr unsigned kSslKeptCapacitors = (1u << 2) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 10);   // C2, C11, C14, C20, C21, C39

ModulePtr makeSslE242 (SslSections sections);
/** channels = 2 gives one stereo module (isStereo(), processStereo()) whose two channels share
    the matrices. */
ModulePtr makeSslE242 (SslSections sections, int channels);

inline constexpr int kSslFilterParamCount = 4;
inline constexpr int kSslEqParamOffset = kSslFilterParamCount;
inline constexpr int kSslEqParamCount = 12;
inline constexpr int kSslEqEnabledParamIndex = kSslEqParamOffset + kSslEqParamCount;

/** The module's controls, in params() order. The ids are the plug-in's parameter ids too. */
inline constexpr std::array<const char*, 17> kSslParamIds {
    "hpf_on", "hpf_freq", "lpf_on", "lpf_freq",
    "hf_gain", "hf_freq", "hf_bell", "hmf_gain", "hmf_freq", "hmf_q",
    "lmf_gain", "lmf_freq", "lmf_q", "lf_gain", "lf_freq", "lf_bell",
    "eq_on" };

/** The controls' specs (ranges, defaults, choices, help), from a module built at 48 kHz. Not
    for the audio thread. */
std::vector<ParamSpec> sslE242Params();

/** Volts at the card for 0 dBFS: a +18 dBu sine's peak, 0.7746 V x 10^(18/20) x sqrt 2. */
inline constexpr float kSslVoltsPerFullScale = 8.7015f;
} // namespace ar::fx
