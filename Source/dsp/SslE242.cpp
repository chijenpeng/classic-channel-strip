// SSL 4000 E-series "black knob" channel EQ card 82E242, the whole card, solved from its
// schematic: SSL drawing T82242.70 issue 8a (June 1986) with the values of the 15 November 1985
// ECO that made Rev. 4 (see docs/reference/schematic.md, docs/research/ssl-eq.md and
// docs/research/82e242-filters.md). The four bands are ported unchanged from the analog-recreation
// project's ssl_e242 module (Source/dsp/fx/SslEq.cpp there). Thirteen NE5534s.
//
// The filter section, its own network ("flt"), solved ahead of the EQ:
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
// open-loop gain is the engine's usual 1500 (a 5534 with 22 pF has about that at 7 kHz; its
// 100 dB DC figure makes Newton crawl), which costs 0.04 dB at flat; the trimmer set-up
// absorbs it in the bands.
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

#include "SslE242.h"
#include "CircuitModule.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ar::fx
{
using namespace ar::circuit;

namespace
{
constexpr float kVoltsPerFullScale = kSslVoltsPerFullScale;   // +18 dBu sine peak: 0.7746 V x 10^(18/20) x sqrt 2
constexpr Real kRailVolts = 16.5;               // 5534 output swing on +-18 V into a light load,
constexpr Real kLinearFraction = 0.9;           // linear to +-14.85 V, then a knee into the rail
constexpr Real kOpenLoopGain = 1500.0;          // the engine's audio-band figure (a 5534 with 22 pF has it near 7 kHz)
constexpr Real kOpAmpOhms = 50.0;
constexpr Real kFetOnOhms = 50.0;               // J112 r_DS(on), at most 50 ohms
constexpr Real kFreqPotOhms = 50e3;             // the 50k "Alog/Alog" frequency pots, as rheostats
constexpr Real kQPotOhms = 10e3;                // the 10k "log log" Q pots
constexpr Real kTopHz = 20000.0;                // the summing amps' 220 pF are pre-warped at the top of the audio band
// A shelf moves the response about 1.7 octaves below (HF) or 1.5 above (LF) the bell centre of
// the same setting (tests/ssl_eq_ac.cpp shelves); its capacitors are pre-warped there.
constexpr double kHfShelfWarp = 1.0 / 3.2, kLfShelfWarp = 2.8;

// Set-up trimmers (ECO: +18 dB at mid frequency, highest Q / bell), from tools/ssl_eq_ac.cpp tables.
constexpr Real kRv1 = 1992.2, kRv2 = 182.87, kRv3 = 176.77, kRv4 = 714.3;

//== Control laws, from tools/ssl_eq_ac.cpp tables ============================================
// Frequency rheostat (both gangs) on this grid of resistances, ohms...
constexpr int kFreqPoints = 21;
constexpr double kRheostat[kFreqPoints] = { 0, 250, 500, 1e3, 1.5e3, 2e3, 3e3, 4e3, 5e3, 6e3, 7.5e3, 10e3, 12.5e3, 15e3, 20e3, 25e3, 30e3, 35e3, 40e3, 45e3, 50e3 };
// ...puts the peak of a full boost (bell, highest Q) here, Hz.
constexpr double kHfHz[kFreqPoints]  = { 19114, 17908, 16825, 15007, 13542, 12336, 10470, 9090, 8028.2, 7184.6, 6198.3, 5017.6, 4161.9, 3534.6, 3009.8, 2627.1, 2146.7, 1837, 1616, 1445.2, 1308 };
constexpr double kHmfHz[kFreqPoints] = { 9421.7, 8823.3, 8286.6, 7388.6, 6666.8, 6073.9, 5157, 4480.6, 3960.8, 3549, 3069.9, 2505.8, 2116.9, 1832.6, 1444.9, 1193, 1016.1, 885.13, 784.25, 704.19, 639.1 };
constexpr double kLmfHz[kFreqPoints] = { 2016.2, 1888.1, 1773.2, 1580.9, 1426.3, 1299.3, 1103.2, 958.7, 847.83, 760.07, 658.1, 538.14, 455.44, 394.97, 312.45, 258.79, 221.16, 193.45, 172.27, 155.52, 141.84 };
constexpr double kLfHz[kFreqPoints]  = { 417.67, 397.02, 378.01, 345.07, 317.48, 294.01, 256.19, 227.02, 203.83, 184.95, 162.4, 134.99, 115.52, 100.97, 80.679, 67.201, 57.599, 50.41, 44.827, 40.365, 36.718 };

// Gain at the band's reference centre against the boost / cut pot, 0 (ACW) .. 1 (CW) in 40
// steps, dB. The centre detent is exactly 0 dB.
constexpr int kGainPoints = 41;
constexpr double kHfDb[kGainPoints] = { -18.0002, -15.8223, -14.1292, -12.7470, -11.5786, -10.5644, -9.6651, -8.8528, -8.1073, -7.4128, -6.7566, -6.1278, -5.5169, -4.9146, -4.3119, -3.6990, -3.0650, -2.3967, -1.6774, -0.8847, 0.0, 0.9136, 1.7088, 2.4300, 3.0998, 3.7347, 4.3481, 4.9511, 5.5533, 6.1639, 6.7919, 7.4472, 8.1403, 8.8842, 9.6944, 10.5913, 11.6024, 12.7670, 14.1443, 15.8312, 18.0000 };
constexpr double kHmfDb[kGainPoints] = { -18.1129, -15.6065, -13.7384, -12.2613, -11.0441, -10.0100, -9.1098, -8.3101, -7.5868, -6.9218, -6.3009, -5.7121, -5.1451, -4.5903, -4.0382, -3.4789, -2.9011, -2.2911, -1.6316, -0.8990, 0.0, 0.7792, 1.5108, 2.1695, 2.7789, 3.3564, 3.9155, 4.4675, 5.0224, 5.5896, 6.1788, 6.8002, 7.4658, 8.1898, 8.9904, 9.8915, 10.9265, 12.1447, 13.6229, 15.4921, 18.0000 };
constexpr double kLmfDb[kGainPoints] = { -18.0751, -15.5231, -13.6298, -12.1386, -10.9142, -9.8771, -8.9769, -8.1792, -7.4594, -6.7992, -6.1840, -5.6018, -5.0422, -4.4954, -3.9520, -3.4019, -2.8339, -2.2343, -1.5857, -0.8643, 0.0, 0.7927, 1.5141, 2.1627, 2.7622, 3.3302, 3.8802, 4.4235, 4.9703, 5.5299, 6.1120, 6.7272, 7.3874, 8.1070, 8.9047, 9.8048, 10.8417, 12.0658, 13.5565, 15.4493, 18.0000 };
constexpr double kLfDb[kGainPoints] = { -18.0735, -15.7689, -14.0084, -12.5896, -11.4023, -10.3803, -9.4804, -8.6728, -7.9357, -7.2524, -6.6096, -5.9961, -5.4018, -4.8175, -4.2340, -3.6413, -3.0283, -2.3817, -1.6845, -0.9137, 0.0, 0.8397, 1.6102, 2.3073, 2.9537, 3.5666, 4.1592, 4.7427, 5.3270, 5.9213, 6.5350, 7.1779, 7.8613, 8.5985, 9.4063, 10.3064, 11.3286, 12.5161, 13.9350, 15.6956, 18.0000 };

// Q (-3 dB bandwidth of a full boost) against the Q pot's travel from its ACW end (both gangs).
constexpr int kQPoints = 16;
constexpr double kQTravel[kQPoints] = { 0, 0.005, 0.01, 0.02, 0.03, 0.05, 0.075, 0.1, 0.15, 0.2, 0.3, 0.4, 0.55, 0.7, 0.85, 1.0 };
constexpr double kHmfQ[kQPoints] = { 4.1272, 3.8020, 3.5016, 3.0335, 2.6855, 2.2031, 1.8211, 1.5685, 1.2550, 1.0682, 0.8558, 0.7381, 0.6349, 0.5724, 0.5301, 0.4994 };
constexpr double kLmfQ[kQPoints] = { 4.2725, 3.9211, 3.5986, 3.1002, 2.7332, 2.2288, 1.8332, 1.5733, 1.2526, 1.0623, 0.8461, 0.7261, 0.6206, 0.5564, 0.5128, 0.4810 };

//== The filter section (docs/research/82e242-filters.md) =====================================
constexpr Real kHpfPotOhms = 47e3;   // the 47k "A/log TRIPLE + SWITCH", each gang a rheostat
constexpr Real kLpfPotOhms = 10e3;   // the 10k "LIN DUAL + SWITCH", each gang a rheostat
constexpr Real kLpfC43 = 6.8e-9;     // C43 6n8: the drawing prints "68n" (decision 2; 68n gives Q 2.25, a +7.3 dB peak)
constexpr Real kSwitchOhms = 0.1;    // a closed contact of the switches on the pots and of EQ IN

// Set-up trimmers (decision 1: K = 2, maximally flat, and 0 dB at 1 kHz with the HPF at mid
// position), from tools/ssl_eq_ac.cpp trims.
constexpr Real kRv5 = 288.15, kRv6 = 1879.60;

// Frequency laws, from tools/ssl_eq_ac.cpp laws: each gang's resistance (ohms) puts the -3 dB
// point, against the filter's own passband, here (Hz).
constexpr int kHpfPoints = 20;
constexpr double kHpfOhms[kHpfPoints] = { 0, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000, 6000, 7500, 10000, 12500, 15000, 20000, 25000, 30000, 35000, 40000, 47000 };
constexpr double kHpfHz[kHpfPoints] = { 394.3, 348.02, 310.82, 256.09, 217.77, 189.44, 150.35, 124.65, 106.46, 92.916, 78.035, 61.613, 50.918, 43.398, 33.523, 27.328, 23.08, 19.985, 17.631, 15.146 };
constexpr int kLpfPoints = 17;
constexpr double kLpfOhms[kLpfPoints] = { 0, 100, 250, 500, 750, 1000, 1500, 2000, 2500, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
constexpr double kLpfHz[kLpfPoints] = { 49675, 43626, 36590, 28839, 23798, 20256, 15611, 12698, 10702, 9247.8, 7271.8, 5991.6, 5094.7, 4431.3, 3920.8, 3515.7, 3186.5 };
// The corner goes as about 1 / (R + the leg's fixed resistor: R55 / R59 / R60 1k8, R69 / R71 680R).
constexpr double kHpfLawOffset = 1800.0, kLpfLawOffset = 680.0;

/** x where the monotonic table ys (over xs) reaches y; clamped to the table's ends. */
double invert (const double* xs, const double* ys, int n, double y) noexcept
{
    const bool rising = ys[n - 1] > ys[0];
    if (rising ? y <= ys[0] : y >= ys[0]) return xs[0];
    if (rising ? y >= ys[n - 1] : y <= ys[n - 1]) return xs[n - 1];
    int i = 0;
    while (i < n - 2 && (rising ? ys[i + 1] < y : ys[i + 1] > y))
        ++i;
    const double t = (y - ys[i]) / (ys[i + 1] - ys[i]);
    return xs[i] + t * (xs[i + 1] - xs[i]);
}

/** Boost / cut pot travel (0 ACW .. 1 CW) for a gain in dB. */
double travelForDb (const double* table, double db) noexcept
{
    static const auto travel = []
    {
        std::array<double, kGainPoints> t {};
        for (int i = 0; i < kGainPoints; ++i) t[(size_t) i] = i / (double) (kGainPoints - 1);
        return t;
    }();
    return invert (travel.data(), table, kGainPoints, db);
}

/** Frequency rheostat resistance for a centre frequency in Hz. The centre goes as about
    1 / (R + 3k6), so the table is interpolated on log (R + 3k6) against log f. */
double rheostatForHz (const double* table, double hz) noexcept
{
    double logR[kFreqPoints], logF[kFreqPoints];
    for (int i = 0; i < kFreqPoints; ++i)
    {
        logR[i] = std::log (kRheostat[i] + 3600.0);
        logF[i] = std::log (table[i]);
    }
    return std::exp (invert (logR, logF, kFreqPoints, std::log (hz))) - 3600.0;
}

/** A filter's rheostat (each gang) for a -3 dB point in Hz, clamped to the pot's ends; the
    table is interpolated on log (R + offset) against log f. */
template <int n>
double filterOhmsForHz (const double (&ohms)[n], const double (&table)[n], double offset, double hz) noexcept
{
    double logR[n], logF[n];
    for (int i = 0; i < n; ++i)
    {
        logR[i] = std::log (ohms[i] + offset);
        logF[i] = std::log (table[i]);
    }
    return std::exp (invert (logR, logF, n, std::log (hz))) - offset;
}

/** Q pot travel from its ACW end for a Q; 1 / Q is close to linear in the travel. */
double travelForQ (const double* table, double q) noexcept
{
    double inv[kQPoints];
    for (int i = 0; i < kQPoints; ++i) inv[i] = 1.0 / table[i];
    return invert (kQTravel, inv, kQPoints, 1.0 / q);
}
} // namespace

class SslE242 final : public CircuitModule
{
public:
    explicit SslE242 (SslSections s = SslSections::all) : sections (s) {}

    std::string id() const override     { return "ssl_e242"; }
    std::string family() const override { return "SSL"; }
    std::string name() const override   { return "4000 E EQ (82E242, filters and EQ)"; }
    bool fullScaleInput() const override { return true; }
    float outputVoltsForFullScale() const override { return 1.0f; }
    /** The summing amps tie every band together through low impedances: one Newton group (and
        the filters' three op-amps another, in their own network). */
    bool mergeGroups() const override { return true; }

    void process (float* samples, int numSamples) override
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] *= kVoltsPerFullScale;          // full scale -> console volts
        CircuitModule::process (samples, numSamples);  // outputGain brings it back
    }

private:
    const SslSections sections;

    /** The 25k linear boost / cut pot with its centre tap: two 12k5 halves, the wiper switched
        onto the half it is on. */
    struct GainPot { int lower, upper, onLower, onUpper; };
    struct Rheostats { int a, b; };

    /** A band's frequency-setting capacitors, pre-warped together where the band acts: at the
        knob frequency (a bell's centre), times shelfRatio on shelf. */
    struct BandWarp
    {
        std::vector<int> caps;
        double shelfRatio = 1.0;
        double hz = 1000.0;   // the knob, set by its binding
        bool shelf = false;   // set by the shape binding

        void apply (LinearNetwork& net) const
        {
            const double warpHz = shelf ? hz * shelfRatio : hz;
            for (int c : caps)
                net.setCapacitorWarp (c, warpHz);
        }
    };
    std::array<BandWarp, 4> warps;   // HF, HMF, LMF, LF

    static GainPot centreTappedPot (LinearNetwork& net, int cw, int acw, int wiper, int tap, const std::string& tag)
    {
        const int xl = net.node (tag + ".xl"), xu = net.node (tag + ".xu");
        GainPot g {};
        g.lower = net.potentiometer (tap, xl, acw, 12.5e3, Taper::linear, 1.0);   // travel 0..0.5: ACW end .. tap
        g.upper = net.potentiometer (cw, xu, tap, 12.5e3, Taper::linear, 0.0);    // travel 0.5..1: tap .. CW end
        g.onLower = net.switchResistor (xl, wiper, 1.0, true);
        g.onUpper = net.switchResistor (xu, wiper, 1.0, false);
        return g;
    }

    static void setGainPot (LinearNetwork& net, const GainPot& g, double travel)
    {
        travel = std::clamp (travel, 0.0, 1.0);
        const bool lower = travel <= 0.5;
        net.setSwitch (g.onLower, lower);
        net.setSwitch (g.onUpper, ! lower);
        if (lower) net.setPot (g.lower, 2.0 * travel);
        else       net.setPot (g.upper, 2.0 * travel - 1.0);
    }

    /** A variable resistor between a and b (a pot with its wiper tied to one end). */
    static int rheostat (LinearNetwork& net, int a, int b, Real ohms, Real initial)
    {
        return net.potentiometer (a, a, b, ohms, Taper::linear, initial / ohms);
    }

    void opAmp (LinearNetwork& net, int inPlus, int inMinus, int out)
    {
        circ.addDevice<OpAmpDevice> (-kRailVolts, kRailVolts, circ.port (net, inPlus), circ.port (net, inMinus), circ.port (net, out),
                                     kOpenLoopGain, kOpAmpOhms, kLinearFraction);
    }

    /** A summing amp: inverting, 4k75 in, 4k75 || 220p feedback. Returns the summing node. */
    int summingAmp (LinearNetwork& net, int in, int out, const std::string& tag)
    {
        const int s = net.node (tag + ".sum");
        net.resistor (in, s, 4.75e3);       // R10 / R38
        net.resistor (out, s, 4.75e3);      // R12 / R35
        net.setCapacitorWarp (net.warpedCapacitor (out, s, 220e-12), kTopHz);   // C8 / C15
        opAmp (net, 0, s, out);             // T3 / T8
        return s;
    }

    /** One band's boost / cut network around `wiper`, between its section's input and output. */
    GainPot boostCut (LinearNetwork& net, int sectionIn, int sectionOut, int wiper, const std::string& tag)
    {
        const int cw = net.node (tag + ".cw"), acw = net.node (tag + ".acw"), tap = net.node (tag + ".tap");
        net.resistor (sectionIn, cw, 1e3);     // R2 / R11 / R25 / R39
        net.resistor (acw, sectionOut, 1e3);   // R15 / R16 / R27 / R40
        net.resistor (tap, 0, 4.7e3);          // centre tap, on the 611 motherboard
        net.resistor (wiper, 0, 1.8e3);        // R3 / R17 / R26 / R41
        return centreTappedPot (net, cw, acw, wiper, tap, tag);
    }

    /** HMF / LMF: band-pass T4 (T6) with Q-pot positive feedback, then T5 (T7). */
    struct MidBand { GainPot gain; Rheostats freq; int qA, qB; std::vector<int> caps; };
    MidBand midBand (LinearNetwork& net, int wiper, int out, Real cap, Real rv, const std::string& tag)
    {
        MidBand b {};
        const int r = net.node (tag + ".r"), c = net.node (tag + ".c"), m = net.node (tag + ".m"), fb = net.node (tag + ".fb");
        const int o = net.node (tag + ".bp"), p = net.node (tag + ".p"), qcw = net.node (tag + ".qcw"), qacw = net.node (tag + ".qacw");
        const int x = net.node (tag + ".x"), s = net.node (tag + ".s"), qb = net.node (tag + ".qb");
        b.freq.a = rheostat (net, wiper, r, kFreqPotOhms, 7.5e3);   // f gang a
        net.resistor (r, c, 3e3);                                   // R18 / R28
        b.caps.push_back (net.warpedCapacitor (c, m, cap));         // C9 / C12
        net.resistor (m, fb, 3.6e3);                                // R19 / R29
        b.freq.b = rheostat (net, fb, o, kFreqPotOhms, 7.5e3);      // f gang b
        b.caps.push_back (net.warpedCapacitor (m, o, cap));         // C10 / C13
        opAmp (net, p, m, o);                                       // T4 / T6
        net.resistor (p, 0, 100e3);                                 // R62 / R98
        // Q gang a: 10k from qacw to qcw, wiper on T4's + input; R21 / R31 across it.
        b.qA = net.potentiometer (qcw, p, qacw, kQPotOhms, Taper::linear, 0.0);
        net.resistor (qcw, qacw, 1.8e3);                            // R21 / R31
        net.resistor (qcw, 0, 470.0);                               // R20 / R30
        net.capacitor (o, x, 100e-6);                               // C11 / C14
        net.resistor (qacw, x, 910.0 + rv);                         // R22 / R32 + RV2 / RV3
        net.resistor (x, s, 750.0);                                 // R23 / R33
        net.resistor (s, qb, 750.0);                                // R24 / R34
        b.qB = rheostat (net, qb, out, kQPotOhms, 0.0);             // Q gang b
        opAmp (net, 0, s, out);                                     // T5 / T7
        return b;
    }

    struct ShelfBellBand { GainPot gain; Rheostats freq; std::vector<int> bellSwitches, shelfSwitches, caps; };

    /** HF: its boost / cut network and R95 from the HMF output, then T1 band-pass / high-pass
        and T2 gain; J112s F1, F2. */
    ShelfBellBand hfBand (LinearNetwork& net, int sectionIn, int sectionOut, int hmfOut, int out)
    {
        ShelfBellBand b {};
        const int w = net.node ("hf.w"), r = net.node ("hf.r"), c = net.node ("hf.c"), m = net.node ("hf.m"), fb = net.node ("hf.fb");
        const int o1 = net.node ("hf.bp"), plus = net.node ("hf.p"), pfb = net.node ("hf.pfb"), f1 = net.node ("hf.f1");
        const int c7 = net.node ("hf.c7"), m2 = net.node ("hf.m2"), c2 = net.node ("hf.c2"), f2 = net.node ("hf.f2");
        b.gain = boostCut (net, sectionIn, sectionOut, w, "hf");
        net.resistor (hmfOut, w, 12e3);                          // R95
        b.freq.a = rheostat (net, w, r, kFreqPotOhms, 7.5e3);    // HF f gang a
        b.caps.push_back (net.warpedCapacitor (w, r, 220e-12));  // C79 across it
        net.resistor (r, c, 3e3);                                // R4
        b.caps.push_back (net.warpedCapacitor (c, m, 2.2e-9));   // C3
        net.resistor (m, fb, 3.6e3);                             // R5
        b.freq.b = rheostat (net, fb, o1, kFreqPotOhms, 7.5e3);  // HF f gang b
        b.caps.push_back (net.warpedCapacitor (m, o1, 220e-12)); // C4
        b.caps.push_back (net.warpedCapacitor (m, pfb, 2.2e-9)); // C5, to the output through C6 and F1
        opAmp (net, plus, m, o1);                                // T1
        net.resistor (plus, 0, 5.1e3);                           // R6
        net.resistor (plus, pfb, 2.4e3 + kRv1);                  // R7 + RV1 (SET HF Q)
        net.capacitor (pfb, f1, 100e-6);                         // C6
        net.resistor (f1, o1, 100e3);                            // R79
        b.bellSwitches.push_back (net.switchResistor (f1, o1, kFetOnOhms, false));   // F1
        net.capacitor (o1, c7, 100e-6);                          // C7
        net.resistor (c7, m2, 3e3);                              // R8
        net.resistor (m2, out, 43e3);                            // R9
        net.resistor (m2, c2, 33e3);                             // R81
        net.capacitor (c2, f2, 2.2e-6);                          // C2
        net.resistor (f2, out, 100e3);                           // R82
        b.bellSwitches.push_back (net.switchResistor (f2, out, kFetOnOhms, false));  // F2
        opAmp (net, 0, m2, out);                                 // T2
        return b;
    }

    /** LF: its boost / cut network and R97 from the LMF output, then T9 band-pass / low-pass
        and T10 gain; J112s F4, F5; bell switch contact 4. */
    ShelfBellBand lfBand (LinearNetwork& net, int sectionIn, int sectionOut, int lmfOut, int out)
    {
        ShelfBellBand b {};
        const int w = net.node ("lf.w"), r = net.node ("lf.r"), c = net.node ("lf.c"), m = net.node ("lf.m"), fb = net.node ("lf.fb");
        const int o9 = net.node ("lf.bp"), plus = net.node ("lf.p"), pfb = net.node ("lf.pfb"), f4 = net.node ("lf.f4");
        const int c21 = net.node ("lf.c21"), m10 = net.node ("lf.m10"), c51 = net.node ("lf.c51"), f5 = net.node ("lf.f5");
        b.gain = boostCut (net, sectionIn, sectionOut, w, "lf");
        net.resistor (lmfOut, w, 12e3);                          // R97
        b.freq.a = rheostat (net, w, r, kFreqPotOhms, 7.5e3);    // LF f gang a
        b.caps.push_back (net.warpedCapacitor (r, c, 33e-9 + 47e-9));    // C16 || C17
        b.shelfSwitches.push_back (net.switchResistor (r, c, 1.0, true));   // contact 4: shorts them on shelf
        net.resistor (c, m, 4.3e3);                              // R42
        net.resistor (m, fb, 4.7e3);                             // R43
        b.freq.b = rheostat (net, fb, o9, kFreqPotOhms, 7.5e3);  // LF f gang b
        b.caps.push_back (net.warpedCapacitor (m, o9, 33e-9 + 47e-9));   // C18 || C19
        opAmp (net, plus, m, o9);                                // T9
        net.resistor (plus, 0, 5.1e3);                           // R44
        net.resistor (plus, pfb, 2.4e3 + kRv4);                  // R45 + RV4 (SET LF Q)
        net.capacitor (pfb, f4, 100e-6);                         // C20
        net.resistor (f4, o9, 100e3);                            // R85
        b.bellSwitches.push_back (net.switchResistor (f4, o9, kFetOnOhms, false));   // F4
        net.capacitor (o9, c21, 100e-6);                         // C21
        net.resistor (c21, m10, 3e3);                            // R47
        net.resistor (m10, out, 43e3);                           // R48
        net.resistor (m10, c51, 7.5e3);                          // R87
        net.capacitor (c51, f5, 100e-6);                         // C51
        net.resistor (f5, out, 100e3);                           // R88
        b.bellSwitches.push_back (net.switchResistor (f5, out, kFetOnOhms, false));  // F5
        opAmp (net, 0, m10, out);                                // T10
        return b;
    }

    /** The card's in / out switch: a contact to the section's output, one to the line past it. */
    struct InOutSwitch { int in, out; };

    /** The control for such a switch: Out (0) or In (1). */
    static std::function<void (float)> bindInOut (LinearNetwork& net, InOutSwitch s)
    {
        return [&net, s] (float v)
        {
            const bool in = v > 0.5f;
            net.setSwitch (s.in, in);
            net.setSwitch (s.out, ! in);
        };
    }

    struct Filters
    {
        int out = 0;                        // the LPF switch's common contact
        std::array<int, 3> hpfGangs {};     // a, b, c of the 47k triple
        std::array<int, 2> lpfGangs {};     // a, b of the 10k dual
        std::array<int, 3> hpfCaps {};      // C34, C36, C37
        std::array<int, 2> lpfCaps {};      // C43, C44
        InOutSwitch hpf {}, lpf {};
    };

    /** The HPF (T11, T12) and LPF (T13) from pin 42 to the LPF switch; docs/research/82e242-filters.md. */
    Filters buildFilters (LinearNetwork& net)
    {
        Filters f;
        // Leaves first, the lines that tie the sections together last (as for the EQ below).
        for (const char* n : { "hpf.x", "hpf.p11", "hpf.h7", "hpf.o11", "hpf.m", "hpf.p12", "hpf.h56", "hpf.h55", "hpf.n12", "hpf.q12",
                               "hpf.o12", "hpf.b12", "hpf.h65", "hpf.h54", "lpf.b13", "lpf.l70", "lpf.l44", "lpf.l45", "lpf.l46", "lpf.p13",
                               "lpf.o13", "lpf.b14", "lpf.l76", "lpf.l47", "in", "p51", "flt.out" })
            net.node (n);
        const int in = net.node ("in"), p51 = net.node ("p51");
        circ.setInput (net, net.voltageSource (in, 0.0));   // pin 42

        //-- HPF, first order: RV5 / R56 into C34 and gang c; T11 a follower ----------------------
        const int x = net.node ("hpf.x"), p11 = net.node ("hpf.p11"), h7 = net.node ("hpf.h7"), o11 = net.node ("hpf.o11");
        net.resistor (in, x, kRv5);                                        // RV5 500R (UNITY GAIN SET), wiper tied to its R56 end
        net.resistor (x, 0, 300.0);                                        // R56
        f.hpfCaps[0] = net.warpedCapacitor (x, p11, 220e-9);               // C34 (C25 N/C)
        net.resistor (p11, 0, 1e6);                                        // R79
        net.resistor (p11, h7, 1.8e3);                                     // R55 -> pin 7
        f.hpfGangs[2] = rheostat (net, h7, 0, kHpfPotOhms, 7.05e3);        // gang c
        opAmp (net, p11, o11, o11);                                        // T11

        //-- HPF, second order: Sallen-Key around T12 ---------------------------------------------
        const int m = net.node ("hpf.m"), p12 = net.node ("hpf.p12"), h56 = net.node ("hpf.h56"), h55 = net.node ("hpf.h55");
        const int n12 = net.node ("hpf.n12"), q12 = net.node ("hpf.q12"), o12 = net.node ("hpf.o12"), b12 = net.node ("hpf.b12");
        const int h65 = net.node ("hpf.h65"), h54 = net.node ("hpf.h54");
        f.hpfCaps[1] = net.warpedCapacitor (o11, m, 220e-9);               // C36
        f.hpfCaps[2] = net.warpedCapacitor (m, p12, 220e-9);               // C37
        net.resistor (m, h56, 1.8e3);                                      // R59 -> pin 56
        f.hpfGangs[0] = rheostat (net, h56, h54, kHpfPotOhms, 7.05e3);     // gang a -> pin 54, after R64: the feedback
        net.resistor (p12, h55, 1.8e3);                                    // R60 -> pin 55
        f.hpfGangs[1] = rheostat (net, h55, 0, kHpfPotOhms, 7.05e3);       // gang b
        opAmp (net, p12, n12, o12);                                        // T12
        net.resistor (o12, n12, 7.5e3);                                    // R58
        net.resistor (n12, q12, kRv6);                                     // RV6 5k (LF FILTER CORNER), a rheostat
        net.resistor (q12, 0, 5.6e3);                                      // R57
        net.capacitor (o12, b12, 220e-6);                                  // C39
        net.resistor (o12, b12, 100e3);                                    // R61 (R63 1M to +18 V only polarises)
        net.capacitor (b12, h65, 220e-6);                                  // C40
        net.resistor (h65, 0, 10e3);                                       // R65
        net.resistor (h65, h54, 10.0);                                     // R64 -> pin 54

        //-- S4: pin 51 from the HPF (pin 54) or the input line (pin 42) --------------------------
        f.hpf.in = net.switchResistor (h54, p51, kSwitchOhms, false);
        f.hpf.out = net.switchResistor (in, p51, kSwitchOhms, true);
        net.resistor (p51, 0, 10e3);                                       // R66

        //-- LPF: unity-gain Sallen-Key around T13 -------------------------------------------------
        const int b13 = net.node ("lpf.b13"), l70 = net.node ("lpf.l70"), l44 = net.node ("lpf.l44"), l45 = net.node ("lpf.l45");
        const int l46 = net.node ("lpf.l46"), p13 = net.node ("lpf.p13"), o13 = net.node ("lpf.o13"), b14 = net.node ("lpf.b14");
        const int l76 = net.node ("lpf.l76"), l47 = net.node ("lpf.l47");
        net.capacitor (p51, b13, 220e-6);                                  // C41
        net.resistor (b13, 0, 100e3);                                      // R68 (R67 1M to +18 V only polarises)
        net.capacitor (b13, l70, 220e-6);                                  // C42
        net.resistor (l70, 0, 10e3);                                       // R70
        net.resistor (l70, l44, 680.0);                                    // R69 -> pin 44
        f.lpfGangs[0] = rheostat (net, l44, l45, kLpfPotOhms, 2e3);        // gang a -> pin 45
        f.lpfGangs[1] = rheostat (net, l45, l46, kLpfPotOhms, 2e3);        // gang b -> pin 46
        net.resistor (l46, p13, 680.0);                                    // R71
        f.lpfCaps[1] = net.warpedCapacitor (p13, 0, 3.3e-9);               // C44
        f.lpfCaps[0] = net.warpedCapacitor (l45, o13, kLpfC43);            // C43, to the "HF FILTER" line
        opAmp (net, p13, o13, o13);                                        // T13
        net.capacitor (o13, b14, 220e-6);                                  // C46
        net.resistor (o13, b14, 100e3);                                    // R72 (R74 1M to +18 V only polarises)
        net.capacitor (b14, l76, 220e-6);                                  // C47
        net.resistor (l76, 0, 10e3);                                       // R76
        net.resistor (l76, l47, 10.0);                                     // R75 -> pin 47

        //-- the LPF's switch: the output from the LPF (pin 47) or pin 51 -------------------------
        f.out = net.node ("flt.out");
        f.lpf.in = net.switchResistor (l47, f.out, kSwitchOhms, false);
        f.lpf.out = net.switchResistor (p51, f.out, kSwitchOhms, true);
        return f;
    }

    void build (double) override
    {
        outputGain = 1.0f / kVoltsPerFullScale;
        LinearNetwork* flt = nullptr;
        Filters filters;
        if (sections != SslSections::eqOnly)
        {
            flt = &circ.addNetwork ("flt");
            filters = buildFilters (*flt);
            addFilterControls (*flt, filters);   // the first four controls
            if (sections == SslSections::filtersOnly)
            {
                circ.setOutput (*flt, filters.out);
                return;
            }
        }
        auto& net = circ.addNetwork ("eq");

        // Nodes are numbered in the order they are first named, and that is the order the LU
        // eliminates them in: each band's inner nodes first, the wipers, band outputs, summing
        // nodes and section lines (which tie everything together) last. The factors then hold
        // 455 non-zeros instead of 1273 in the order the circuit is drawn.
        for (const char* n : { "hf.c2", "hf.f2", "hf.c7", "hf.f1", "hf.p", "hf.pfb", "hf.fb", "hf.c", "hf.r", "hf.m", "hf.bp", "hf.m2",
                               "hf.tap", "hf.xl", "hf.xu", "hf.cw", "hf.acw" })
            net.node (n);
        for (const char* tag : { "hmf", "lmf" })
            for (const char* n : { ".qb", ".s", ".x", ".qcw", ".qacw", ".p", ".fb", ".c", ".r", ".m", ".bp", ".tap", ".xl", ".xu", ".cw", ".acw" })
                net.node (std::string (tag) + n);
        for (const char* n : { "lf.c51", "lf.f5", "lf.c21", "lf.f4", "lf.p", "lf.pfb", "lf.fb", "lf.c", "lf.r", "lf.m", "lf.bp", "lf.m10",
                               "lf.tap", "lf.xl", "lf.xu", "lf.cw", "lf.acw" })
            net.node (n);
        for (const char* n : { "in", "in.j", "hf.w", "hmf.w", "hf.out", "hmf.out", "in.a", "t3.sum",
                               "lmf.w", "lf.w", "lmf.out", "lf.out", "t3.out", "t8.sum", "t8.out", "out" })
            net.node (n);

        //-- input: pin 48 -> C22 220u -> (R1 100k to ground) -> C1 220u -----------------------
        // R50 (1M to +18 V) only polarises the electrolytics; R100 47k loads the pin. The pin is
        // driven from the filter section's output through the 611's buffering, taken as ideal.
        const int in = net.node ("in"), j = net.node ("in.j"), a = net.node ("in.a");
        if (flt != nullptr)
            circ.addBuffer (*flt, filters.out, net, net.voltageSource (in, 0.0));
        else
            circ.setInput (net, net.voltageSource (in, 0.0));
        net.capacitor (in, j, 220e-6);
        net.resistor (j, 0, 100e3);
        net.capacitor (j, a, 220e-6);

        //== section 1: T3 sums HF and HMF ======================================================
        const int o3 = net.node ("t3.out"), hfOut = net.node ("hf.out"), hmfOut = net.node ("hmf.out");
        const int s3 = summingAmp (net, a, o3, "t3");
        net.resistor (hmfOut, s3, 4.75e3);   // R13
        net.resistor (hfOut, s3, 4.75e3);    // R14

        //-- HF ----------------------------------------------------------------------------------
        const auto hf = hfBand (net, a, o3, hmfOut, hfOut);

        //-- HMF: 4n7 ----------------------------------------------------------------------------
        const int hmfW = net.node ("hmf.w");
        auto hmf = midBand (net, hmfW, hmfOut, 4.7e-9, kRv2, "hmf");
        hmf.gain = boostCut (net, a, o3, hmfW, "hmf");
        net.resistor (hfOut, hmfW, 12e3);                            // R94

        //== section 2: T8 sums LMF and LF ======================================================
        const int o8 = net.node ("t8.out"), lmfOut = net.node ("lmf.out"), lfOut = net.node ("lf.out");
        const int s8 = summingAmp (net, o3, o8, "t8");
        net.resistor (lmfOut, s8, 4.75e3);   // R36
        net.resistor (lfOut, s8, 4.75e3);    // R37

        //-- LMF: 22n ----------------------------------------------------------------------------
        const int lmfW = net.node ("lmf.w");
        auto lmf = midBand (net, lmfW, lmfOut, 22e-9, kRv3, "lmf");
        lmf.gain = boostCut (net, o3, o8, lmfW, "lmf");
        net.resistor (lfOut, lmfW, 12e3);                            // R96

        //-- LF ----------------------------------------------------------------------------------
        const auto lf = lfBand (net, o3, o8, lmfOut, lfOut);

        //-- output: C23 + C24 (2 x 220u in series) into R53 10k; R51 / R52 bias the caps ---------
        const int out = net.node ("out");
        net.capacitor (o8, out, 110e-6);
        net.resistor (out, 0, 10e3);

        warps[0] = { hf.caps, kHfShelfWarp };
        warps[1] = { hmf.caps };
        warps[2] = { lmf.caps };
        warps[3] = { lf.caps, kLfShelfWarp };
        addControls (net, hf, hmf, lmf, lf);

        //-- EQ IN: the card's output (pin 43) or the line from pin 48 past it ---------------------
        if (flt == nullptr)
        {
            circ.setOutput (net, out);   // the four bands alone have no EQ IN switch
            return;
        }
        const int cardOut = net.node ("card.out");
        const InOutSwitch eqIn { net.switchResistor (out, cardOut, kSwitchOhms, true), net.switchResistor (in, cardOut, kSwitchOhms, false) };
        circ.setOutput (net, cardOut);
        addParam (ParamSpec::choice ("eq_on", "EQ", { "Out", "In" }, 1, "The EQ IN switch: Out passes the signal past the four bands (the filters stay as set)."),
                  bindInOut (net, eqIn), false);
    }

    /** HPF in / frequency, LPF in / frequency (ids and order are saved in sessions). */
    void addFilterControls (LinearNetwork& net, const Filters& f)
    {
        const auto bindCorner = [&net] (auto gangs, auto caps, const auto& ohmsLaw, const auto& hzLaw, double lawOffset, Real potOhms)
        {
            return [&net, gangs, caps, &ohmsLaw, &hzLaw, lawOffset, potOhms] (float hz)
            {
                // Every gang where the circuit has its -3 dB point on the knob, the filter's
                // capacitors pre-warped at that corner (where the pots reach).
                const double ohms = std::clamp (filterOhmsForHz (ohmsLaw, hzLaw, lawOffset, hz), 0.0, potOhms);
                for (int g : gangs) net.setPot (g, ohms / potOhms);
                const double corner = std::clamp ((double) hz, hzLaw[std::size (hzLaw) - 1], hzLaw[0]);
                for (int c : caps) net.setCapacitorWarp (c, corner);
            };
        };

        addParam (ParamSpec::choice ("hpf_on", "HPF", { "Out", "In" }, 0, "The high-pass filter's switch (on the pot on the console)."), bindInOut (net, f.hpf), false);
        addParam (ParamSpec::frequency ("hpf_freq", "HPF freq", 80.0f, "High-pass corner: the -3 dB point of the 18 dB/oct filter. The circuit reaches 15.1-394 Hz.", 16.0f, 390.0f),
                  bindCorner (f.hpfGangs, f.hpfCaps, kHpfOhms, kHpfHz, kHpfLawOffset, kHpfPotOhms));
        addParam (ParamSpec::choice ("lpf_on", "LPF", { "Out", "In" }, 0, "The low-pass filter's switch (on the pot on the console)."), bindInOut (net, f.lpf), false);
        addParam (ParamSpec::frequency ("lpf_freq", "LPF freq", 12000.0f, "Low-pass corner: the -3 dB point of the 12 dB/oct filter. The circuit reaches 3.19-49.7 kHz.", 3200.0f, 22000.0f),
                  bindCorner (f.lpfGangs, f.lpfCaps, kLpfOhms, kLpfHz, kLpfLawOffset, kLpfPotOhms));
    }

    /** The controls: HF, HMF, LMF, LF; gain, frequency, then Q or shape (ids and order are
        saved in sessions). */
    void addControls (LinearNetwork& net, const ShelfBellBand& hf, const MidBand& hmf, const MidBand& lmf, const ShelfBellBand& lf)
    {
        const auto gainSpec = [] (const std::string& id, const std::string& band, const std::string& help)
        {
            auto s = ParamSpec::knob (id, band + " gain", 0.0f, help, -18.0f, 18.0f);
            s.unit = "dB";
            return s;
        };
        const auto qSpec = [] (const std::string& id, const std::string& band, float top, const std::string& topText)
        {
            auto s = ParamSpec::knob (id, band + " Q", 1.5f,
                                      band + " bandwidth: the Q of a full boost (-3 dB points), 0.5 wide to " + topText
                                          + " narrow. Narrow also gives the most range (+-18 dB), wide about +-15.5 dB.", 0.5f, top);
            s.logScale = true;
            return s;
        };
        const auto bindGain = [&net] (GainPot g, const double* table) { return [&net, g, table] (float db) { setGainPot (net, g, travelForDb (table, db)); }; };
        const auto bindFreq = [&net] (Rheostats r, const double* table, BandWarp& warp)
        {
            return [&net, r, table, &warp] (float hz)
            {
                // The pots where the console has them for this frequency; the band's capacitors
                // pre-warped there, so the centre lands on it at any rate.
                const double ohms = std::clamp (rheostatForHz (table, hz), 0.0, kFreqPotOhms);
                net.setPot (r.a, ohms / kFreqPotOhms);
                net.setPot (r.b, ohms / kFreqPotOhms);
                warp.hz = hz;
                warp.apply (net);
            };
        };
        const auto bindQ = [&net] (const MidBand& b, const double* table)
        {
            return [&net, qA = b.qA, qB = b.qB, table] (float q)
            {
                const double travel = travelForQ (table, q);
                net.setPot (qA, travel);   // wiper fraction from the ACW end (T4's positive feedback)
                net.setPot (qB, travel);   // and the rheostat in T5's feedback
            };
        };
        const auto bindShape = [&net] (const ShelfBellBand& b, BandWarp& warp)
        {
            return [&net, &warp, bell = b.bellSwitches, shelf = b.shelfSwitches] (float v)
            {
                const bool on = v > 0.5f;
                for (int s : bell) net.setSwitch (s, on);
                for (int s : shelf) net.setSwitch (s, ! on);
                warp.shelf = ! on;
                warp.apply (net);
            };
        };

        addParam (gainSpec ("hf_gain", "HF", "HF boost or cut, in dB at the peak of the bell (+-18). The shelf gives about +-15.5 dB at full travel."),
                  bindGain (hf.gain, kHfDb));
        addParam (ParamSpec::frequency ("hf_freq", "HF freq", 8000.0f, "HF centre: where the bell peaks. On shelf the same setting is 1-2 dB short of the full shelf "
                                        "there and half way about 1.7 octaves lower.", 1300.0f, 19000.0f),
                  bindFreq (hf.freq, kHfHz, warps[0]));
        addParam (ParamSpec::choice ("hf_bell", "HF shape", { "Shelf", "Bell" }, 0, "HF as a 6 dB/oct shelf or a bell (Q about 1), the console's BELL button."),
                  bindShape (hf, warps[0]), false);
        addParam (gainSpec ("hmf_gain", "HMF", "HMF boost or cut, in dB at the centre: +-18 at the narrowest Q, about +-15.5 at the wider ones."),
                  bindGain (hmf.gain, kHmfDb));
        addParam (ParamSpec::frequency ("hmf_freq", "HMF freq", 2500.0f, "HMF centre frequency.", 640.0f, 9400.0f),
                  bindFreq (hmf.freq, kHmfHz, warps[1]));
        addParam (qSpec ("hmf_q", "HMF", 4.1f, "4.1"), bindQ (hmf, kHmfQ));
        addParam (gainSpec ("lmf_gain", "LMF", "LMF boost or cut, in dB at the centre: +-18 at the narrowest Q, about +-15.5 at the wider ones."),
                  bindGain (lmf.gain, kLmfDb));
        addParam (ParamSpec::frequency ("lmf_freq", "LMF freq", 500.0f, "LMF centre frequency.", 140.0f, 2000.0f), bindFreq (lmf.freq, kLmfHz, warps[2]));
        addParam (qSpec ("lmf_q", "LMF", 4.2f, "4.2"), bindQ (lmf, kLmfQ));
        addParam (gainSpec ("lf_gain", "LF", "LF boost or cut, in dB at the peak of the bell (+-18). The shelf gives about +-16.5 dB at full travel."),
                  bindGain (lf.gain, kLfDb));
        addParam (ParamSpec::frequency ("lf_freq", "LF freq", 100.0f, "LF centre: where the bell peaks. On shelf the same setting is about 2.5 dB short of the full "
                                        "shelf there and half way about 1.5 octaves higher.", 37.0f, 420.0f),
                  bindFreq (lf.freq, kLfHz, warps[3]));
        addParam (ParamSpec::choice ("lf_bell", "LF shape", { "Shelf", "Bell" }, 0, "LF as a 6 dB/oct shelf or a bell (Q about 2), the console's BELL button."),
                  bindShape (lf, warps[3]), false);
    }
};

ModulePtr makeSslE242()
{
    return std::make_unique<SslE242>();
}

ModulePtr makeSslE242 (SslSections sections)
{
    return std::make_unique<SslE242> (sections);
}

std::vector<ParamSpec> sslE242Params()
{
    SslE242 m;
    m.prepare (48000.0);
    return m.params();
}
} // namespace ar::fx
