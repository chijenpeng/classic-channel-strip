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
ModulePtr makeSslE242 (SslSections sections);

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
