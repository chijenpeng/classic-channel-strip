// SSL 4000 E-series "black knob" channel EQ card 82E242, the whole card, solved from its
// schematic: SSL drawing T82242.70 issue 8a (June 1986) with the values of the 15 November 1985
// ECO that made Rev. 4 (see docs/reference/schematic.md, docs/research/ssl-eq.md and
// docs/research/82e242-filters.md). The four bands are ported unchanged from the analog-recreation
// project's ssl_e242 module (Source/dsp/fx/SslEq.cpp there). Thirteen NE5534s.
//
// The filter section, solved ahead of the EQ (its own networks, HPF then LPF):
//  - HPF, 18 dB/oct: RV5 (UNITY GAIN SET) and R56 divide the input into C34 and gang c of the
//    47k triple (first order, T11 a follower), then an equal-component Sallen-Key high-pass
//    around T12 (C36, C37, gangs a and b) whose gain K = 1 + R58 / (R57 + RV6) sets its Q.
//    RV6 is set for K = 2 (Q 1: with the first order a third-order Butterworth) and RV5 for 0 dB
//    at the pot's mid position (decision 1 of the filter note).
//  - LPF, 12 dB/oct: a unity-gain Sallen-Key low-pass around T13 (R69 / R71 680R with the two
//    gangs of the 10k dual, C43, C44). C43 is 6n8 although the drawing prints 68n (decision 2).
//  - The switches on the pots (S4 and the LPF's) select the filter's output or the line that
//    bypasses it; the EQ IN switch selects the card's output or its input line. All three are
//    the card's switches, hard ones, in the netlist.
//  - The 611 motherboard's buffering between the filter loop (pins 42-47) and the EQ loop (pins
//    48-43) is taken as ideal: a unity buffer from the filter output into the EQ input.
//
// The EQ, ten NE5534s:
//  - T3 sums HF and HMF, T8 sums LMF and LF; the two sections are inverting and in series.
//  - Each band: a 25k linear boost / cut pot between its section's input (CW, boost) and
//    output (ACW, cut) through 1k0s, centre tap to ground through 4k7 on the motherboard, 1k8
//    from the wiper to ground. The wiper drives the band's filter, whose output is summed back
//    into the section through 4k75: boost puts it in the forward path, cut in the feedback.
//  - HF and LF: a multiple-feedback band-pass (T1 / T9) whose Q is raised by positive feedback
//    (Deliyannis), then a gain stage (T2 / T10). Two J112s switch the positive feedback and the
//    gain stage's feedback leg: bell when on; off, T1 is a high-pass and T9 (with the bell
//    switch shorting its input capacitors) a low-pass, so the band is a 6 dB/oct shelf.
//  - HMF and LMF: the same band-pass (T4 / T6), its positive feedback set by the Q pot's
//    first gang, into an inverting stage (T5 / T7) whose gain the second gang raises as the
//    Q falls.
//  - Each band's output also reaches the other band of its section through 12k into the
//    wiper (R94-R97); the 82E02 brown knob card has the same mod on the back of the PCB.
//
// The EQ trimmers (RV1-RV4) are set the way the ECO says: +18 dB of boost with the frequency pot
// at mid travel and the highest Q / bell.
//
// Controls are in the console's units, mapped to pot positions by laws computed from the
// circuit (tools/ssl_eq_ac.cpp, an AC analysis of the same netlist): the filters' frequency in
// Hz at their -3 dB point (against their own passband); the bands' frequency in Hz at the
// peak of a full boost (a shelf uses the same pot position as the bell would), Q as the
// -3 dB bandwidth of a full boost (SSL's figure: 0.5-4.5 on the ECO), gain in dB at the
// set-up reference. Away from the reference the circuit's own behaviour shows: +-15 dB at the
// lowest Q, +-15 to 16.5 dB on the shelves, the centre frequency moving a few percent.
//
// Level: 1.0 full scale is 0 dBFS = +18 dBu (the EBU R68 alignment, -18 dBFS = 0 dBu), 8.70 V
// peak, so 0 VU (+4 dBu) sits at -14 dBFS. The 5534s on +-18 V are linear to +-14.85 V and saturate at
// +-16.5 V, so a flat EQ stays clean to +4.6 dBFS and a band boosted by b dB starts to clip
// where that band's content is above (4.6 - b) dBFS, as on a console aligned that way. Their
// open-loop gain is taken as 1500 (a 5534 with 22 pF has about that at 7 kHz), which costs
// 0.04 dB at flat; the trimmer set-up absorbs it in the bands.
//
// It is an analog circuit model: it keeps its analog phase and adds no latency. The trapezoidal
// rule squeezes the analog frequency axis into the band below Nyquist, so capacitors are
// pre-warped where they act (LinearNetwork::setCapacitorWarp): each band's frequency-setting
// capacitors together at the bell's centre (the knob), or a shelf's half-way point, each
// filter's at its corner (the knob), and the summing amps' 220 pF at 20 kHz. A filter's corner
// then lands on the knob at every rate, and a bell peaks on the knob with the
// schematic's gain at every rate, over the pots' whole range, and the flat response is the
// schematic's to 20 kHz. What no step of the rule can give at 44.1 / 48 kHz is the console's
// response around a bell centred high up: the response at Nyquist is the circuit's at infinite
// frequency, so such a bell is narrower than the hardware's, most of all on its upper side. 2x
// brings it within about a dB (see the research notes).

#include "SslE242Card.h"

#include <memory>

namespace ar::fx
{
ModulePtr makeSslE242()
{
    return std::make_unique<SslE242> (SslSections::all, 1);
}

ModulePtr makeSslE242 (SslSections sections)
{
    return std::make_unique<SslE242> (sections, 1);
}

ModulePtr makeSslE242 (SslSections sections, int channels)
{
    return std::make_unique<SslE242> (sections, channels);
}

std::vector<ParamSpec> sslE242Params()
{
    SslE242 m (SslSections::all, 1);
    m.prepare (48000.0);
    return m.params();
}
} // namespace ar::fx
