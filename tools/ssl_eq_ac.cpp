// Small-signal AC analysis of the SSL 82E242 "black knob" card (drawing T82242.70 issue 8a, the
// 15 Nov 1985 ECO's values): nodal analysis with complex admittances and the op-amps as
// voltage-controlled sources (gain 1500 behind 50 ohms, as in the circuit model). It is the
// schematic's own frequency response, with no time stepping, and the source of the trimmer
// values and control-law tables in Source/dsp/SslE242.cpp. Ported from the analog-recreation
// project (tests/ssl_eq_ac.cpp there, the four EQ bands) and extended with the card's HPF / LPF
// section (docs/research/82e242-filters.md). Built by CMake as classic_channel_eq_ac.
//
// The EQ bands:
//   classic_channel_eq_ac tables      trimmer set-up (ECO: +18 dB at mid frequency, highest Q / bell) and
//                         the frequency, gain and Q laws, printed as C++
//   classic_channel_eq_ac spec        ranges, boost / cut and Q at the pot ends, against the ECO
//   classic_channel_eq_ac shelves     where the shelves sit against the bell centre of the same setting
//   classic_channel_eq_ac headroom    largest gain from the input to every op-amp output
//   classic_channel_eq_ac lfq         the LF bell's set-up: Q at +18 dB (the ECO's procedure) against the
//                         gain at SSL's Q 1.7, with the op-amps' gain at 1500 and 1e5
//   classic_channel_eq_ac curve band gain-travel rheostat-ohms bell-or-Q-travel
//                         response from 10 Hz to 20 kHz (band 0 HF, 1 HMF, 2 LMF, 3 LF)
//
// The filters (HPF T11 / T12, LPF T13, with the card's in / out switches):
//   classic_channel_eq_ac trims       RV6 for K = 2 (Q 1, maximally flat) and RV5 for unity passband gain,
//                         printed as C++ (decision 1 of docs/research/82e242-filters.md)
//   classic_channel_eq_ac laws        the HPF and LPF frequency laws (rheostat -> -3 dB point), as C++,
//                         and how well the module's interpolation of them lands
//   classic_channel_eq_ac filters     each filter at a few settings (passband, -3 dB, slopes, peaking, Q),
//                         the ranges, headroom, and the whole card flat
//   classic_channel_eq_ac lpf68n      the LPF with C43 = 68n as the drawing prints it, against 6n8
//   classic_channel_eq_ac fcurve hpf-in hpf-ohms lpf-in lpf-ohms [c43-farads]
//                         the filter section's response from 10 Hz to 40 kHz
//
// EQ pots: the boost / cut pot's travel runs 0 (ACW, full cut) to 1 (CW, full boost); the
// frequency pots are rheostats of 0..50k (both gangs alike); the Q pot's travel is the wiper's
// fraction from its ACW end (0 is the highest Q). The other bands sit at a gain travel of 0.5,
// 7k5 and the highest Q unless a mode says otherwise. Filter pots: each gang of the HPF's 47k
// triple and of the LPF's 10k dual is a rheostat (wiper tied to one end), all gangs alike.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
using Complex = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

/** Nodal analysis with ideal voltage sources and voltage-controlled sources (extra rows). */
struct Net
{
    struct Element { char type; int a, b, c; double value; };
    std::map<std::string, int> index;
    std::vector<Element> elements;
    double opAmpGain = 1500.0, opAmpOhms = 50.0;

    int node (const std::string& name)
    {
        if (name == "0") return 0;
        const auto it = index.find (name);
        if (it != index.end()) return it->second;
        const int k = (int) index.size() + 1;
        index[name] = k;
        return k;
    }
    void R (const std::string& a, const std::string& b, double ohms)   { elements.push_back ({ 'R', node (a), node (b), 0, ohms }); }
    void C (const std::string& a, const std::string& b, double farads) { elements.push_back ({ 'C', node (a), node (b), 0, farads }); }
    /** out = opAmpGain (plus - minus) behind opAmpOhms. */
    void opAmp (const std::string& plus, const std::string& minus, const std::string& out)
    {
        elements.push_back ({ 'O', node (plus), node (minus), node (out + ".int"), opAmpGain });
        R (out + ".int", out, opAmpOhms);
    }
    void source (const std::string& a) { elements.push_back ({ 'V', node (a), 0, 0, 1.0 }); }

    /** Node voltages at f for a 1 V source. */
    std::vector<Complex> solve (double f) const
    {
        const int n = (int) index.size();
        int extra = 0;
        for (const auto& e : elements) extra += (e.type == 'O' || e.type == 'V') ? 1 : 0;
        const int dim = n + extra;
        std::vector<Complex> a ((size_t) dim * (size_t) dim, 0.0), b ((size_t) dim, 0.0);
        auto at = [&] (int r, int c) -> Complex& { return a[(size_t) r * (size_t) dim + (size_t) c]; };
        auto stamp = [&] (int p, int q, Complex y)
        {
            if (p) at (p - 1, p - 1) += y;
            if (q) at (q - 1, q - 1) += y;
            if (p && q) { at (p - 1, q - 1) -= y; at (q - 1, p - 1) -= y; }
        };
        const Complex s (0.0, 2.0 * kPi * f);
        int row = n;
        for (const auto& e : elements)
        {
            if (e.type == 'R') stamp (e.a, e.b, 1.0 / e.value);
            else if (e.type == 'C') stamp (e.a, e.b, s * e.value);
            else if (e.type == 'O')
            {
                at (e.c - 1, row) += 1.0;
                at (row, e.c - 1) += 1.0;
                if (e.a) at (row, e.a - 1) -= e.value;
                if (e.b) at (row, e.b - 1) += e.value;
                ++row;
            }
            else
            {
                at (e.a - 1, row) += 1.0;
                at (row, e.a - 1) += 1.0;
                b[(size_t) row] = e.value;
                ++row;
            }
        }
        for (int i = 0; i < dim; ++i)
        {
            int p = i;
            for (int r = i + 1; r < dim; ++r) if (std::abs (at (r, i)) > std::abs (at (p, i))) p = r;
            if (p != i) { for (int c = 0; c < dim; ++c) std::swap (at (i, c), at (p, c)); std::swap (b[(size_t) i], b[(size_t) p]); }
            for (int r = i + 1; r < dim; ++r)
            {
                const Complex m = at (r, i) / at (i, i);
                if (m == 0.0) continue;
                for (int c = i; c < dim; ++c) at (r, c) -= m * at (i, c);
                b[(size_t) r] -= m * b[(size_t) i];
            }
        }
        std::vector<Complex> x ((size_t) dim);
        for (int i = dim - 1; i >= 0; --i)
        {
            Complex acc = b[(size_t) i];
            for (int c = i + 1; c < dim; ++c) acc -= at (i, c) * x[(size_t) c];
            x[(size_t) i] = acc / at (i, i);
        }
        std::vector<Complex> v ((size_t) n + 1, 0.0);
        for (int i = 1; i <= n; ++i) v[(size_t) i] = x[(size_t) i - 1];
        return v;
    }
};

struct Settings
{
    double gain[4] = { 0.5, 0.5, 0.5, 0.5 };          // HF, HMF, LMF, LF boost / cut travel
    double rheostat[4] = { 7.5e3, 7.5e3, 7.5e3, 7.5e3 };
    double qHmf = 0.0, qLmf = 0.0;                      // Q travel from the ACW end
    bool bellHf = false, bellLf = false;
    double rv1 = 1992.2, rv2 = 182.87, rv3 = 176.77, rv4 = 714.3;   // set-up trimmers (see `tables`)
    double r87 = 7.5e3;                                 // LF bell gain (7k5 on the drawing and in the ECO)
    double opAmpGain = 1500.0;
};

/** The 25k linear boost / cut pot, centre tap at `tap`, wiper at travel 0 (ACW) .. 1 (CW). */
void centreTappedPot (Net& n, const std::string& cw, const std::string& acw, const std::string& w, const std::string& tap, double travel)
{
    const double total = 25e3, half = total * 0.5, x = travel * total, least = 0.5;
    if (x <= half) { n.R (acw, w, std::max (least, x)); n.R (w, tap, std::max (least, half - x)); n.R (tap, cw, half); }
    else           { n.R (acw, tap, half); n.R (tap, w, std::max (least, x - half)); n.R (w, cw, std::max (least, total - x)); }
}

Net build (const Settings& s)
{
    Net n;
    n.opAmpGain = s.opAmpGain;
    const double fetOff = 1e12, fetOn = 50.0;   // J112
    n.source ("in");
    n.C ("in", "j", 220e-6);              // C22
    n.R ("j", "0", 100e3);                // R1 (R50 only polarises)
    n.C ("j", "A", 220e-6);               // C1
    //== section 1: T3 sums HF and HMF =========================================================
    n.R ("A", "S3", 4.75e3);              // R10
    n.R ("O3", "S3", 4.75e3);             // R12
    n.C ("O3", "S3", 220e-12);            // C8
    n.R ("BM", "S3", 4.75e3);             // R13
    n.R ("BH", "S3", 4.75e3);             // R14
    n.opAmp ("0", "S3", "O3");            // T3
    //-- HF ----------------------------------------------------------------------------------
    n.R ("A", "h4", 1e3);                 // R2, CW end
    n.R ("h3", "O3", 1e3);                // R15, ACW end
    centreTappedPot (n, "h4", "h3", "WH", "hC", s.gain[0]);
    n.R ("hC", "0", 4.7e3);               // centre tap, motherboard
    n.R ("WH", "0", 1.8e3);               // R3
    n.R ("BM", "WH", 12e3);               // R95
    n.R ("WH", "h9", std::max (5.0, s.rheostat[0]));   // HF f a
    n.C ("WH", "h9", 220e-12);            // C79
    n.R ("h9", "h9b", 3e3);               // R4
    n.C ("h9b", "S1", 2.2e-9);            // C3
    n.R ("S1", "h6", 3.6e3);              // R5
    n.R ("h6", "O1", std::max (5.0, s.rheostat[0]));   // HF f b
    n.C ("S1", "O1", 220e-12);            // C4
    n.C ("S1", "P1", 2.2e-9);             // C5
    n.opAmp ("Pp1", "S1", "O1");          // T1
    n.R ("Pp1", "0", 5.1e3);              // R6
    n.R ("Pp1", "P1", 2.4e3 + s.rv1);     // R7 + RV1
    n.C ("P1", "F1", 100e-6);             // C6
    n.R ("F1", "O1", s.bellHf ? fetOn : fetOff);   // F1
    n.R ("F1", "O1", 100e3);              // R79
    n.C ("O1", "h7", 100e-6);             // C7
    n.R ("h7", "S2", 3e3);                // R8
    n.R ("S2", "BH", 43e3);               // R9
    n.R ("S2", "c2", 33e3);               // R81
    n.C ("c2", "F2", 2.2e-6);             // C2
    n.R ("F2", "BH", s.bellHf ? fetOn : fetOff);   // F2
    n.R ("F2", "BH", 100e3);              // R82
    n.opAmp ("0", "S2", "BH");            // T2
    //-- HMF (LMF below is the same with 22n) -------------------------------------------------
    auto mid = [&] (const std::string& t, const std::string& in, const std::string& out, const std::string& w, const std::string& outNode,
                    const std::string& other, double gain, double rheostat, double q, double cap, double rv)
    {
        n.R (in, t + "cw", 1e3);          // R11 / R25
        n.R (t + "acw", out, 1e3);        // R16 / R27
        centreTappedPot (n, t + "cw", t + "acw", w, t + "C", gain);
        n.R (t + "C", "0", 4.7e3);        // centre tap
        n.R (w, "0", 1.8e3);              // R17 / R26
        n.R (other, w, 12e3);             // R94 / R96
        n.R (w, t + "r", std::max (5.0, rheostat));
        n.R (t + "r", t + "c", 3e3);      // R18 / R28
        n.C (t + "c", t + "m", cap);      // C9 / C12
        n.R (t + "m", t + "fb", 3.6e3);   // R19 / R29
        n.R (t + "fb", t + "bp", std::max (5.0, rheostat));
        n.C (t + "m", t + "bp", cap);     // C10 / C13
        n.opAmp (t + "p", t + "m", t + "bp");   // T4 / T6
        n.R (t + "p", "0", 100e3);        // R62 / R98
        n.R (t + "qacw", t + "p", std::max (5.0, 10e3 * q));          // Q gang a, wiper q from ACW
        n.R (t + "p", t + "qcw", std::max (5.0, 10e3 * (1.0 - q)));
        n.R (t + "qcw", t + "qacw", 1.8e3);   // R21 / R31
        n.R (t + "qcw", "0", 470.0);          // R20 / R30
        n.R (t + "qacw", t + "x", 910.0 + rv);   // R22 / R32 + RV2 / RV3
        n.C (t + "bp", t + "x", 100e-6);   // C11 / C14
        n.R (t + "x", t + "s", 750.0);     // R23 / R33
        n.R (t + "s", t + "qb", 750.0);    // R24 / R34
        n.R (t + "qb", outNode, std::max (5.0, 10e3 * q));   // Q gang b
        n.opAmp ("0", t + "s", outNode);   // T5 / T7
    };
    mid ("m", "A", "O3", "WM", "BM", "BH", s.gain[1], s.rheostat[1], s.qHmf, 4.7e-9, s.rv2);
    //== section 2: T8 sums LMF and LF =========================================================
    n.R ("O3", "S8", 4.75e3);             // R38
    n.R ("O8", "S8", 4.75e3);             // R35
    n.C ("O8", "S8", 220e-12);            // C15
    n.R ("BL", "S8", 4.75e3);             // R36
    n.R ("BF", "S8", 4.75e3);             // R37
    n.opAmp ("0", "S8", "O8");            // T8
    mid ("l", "O3", "O8", "WL", "BL", "BF", s.gain[2], s.rheostat[2], s.qLmf, 22e-9, s.rv3);
    //-- LF ----------------------------------------------------------------------------------
    n.R ("O3", "f41", 1e3);               // R39
    n.R ("f40", "O8", 1e3);               // R40
    centreTappedPot (n, "f41", "f40", "WF", "fC", s.gain[3]);
    n.R ("fC", "0", 4.7e3);
    n.R ("WF", "0", 1.8e3);               // R41
    n.R ("BL", "WF", 12e3);               // R97
    n.R ("WF", "f34", std::max (5.0, s.rheostat[3]));
    n.C ("f34", "f37", 33e-9 + 47e-9);    // C16 || C17
    n.R ("f34", "f37", s.bellLf ? 1e12 : 1.0);   // switch contact 4: closed on shelf
    n.R ("f37", "S9", 4.3e3);             // R42
    n.R ("S9", "f35", 4.7e3);             // R43
    n.R ("f35", "O9", std::max (5.0, s.rheostat[3]));
    n.C ("S9", "O9", 33e-9 + 47e-9);      // C18 || C19
    n.opAmp ("fp", "S9", "O9");           // T9
    n.R ("fp", "0", 5.1e3);               // R44
    n.R ("fp", "PL", 2.4e3 + s.rv4);      // R45 + RV4
    n.C ("PL", "F4", 100e-6);             // C20
    n.R ("F4", "O9", s.bellLf ? fetOn : fetOff);   // F4
    n.R ("F4", "O9", 100e3);              // R85
    n.C ("O9", "f21", 100e-6);            // C21
    n.R ("f21", "S10", 3e3);              // R47
    n.R ("S10", "BF", 43e3);              // R48
    n.R ("S10", "c51", s.r87);            // R87
    n.C ("c51", "F5", 100e-6);            // C51
    n.R ("F5", "BF", s.bellLf ? fetOn : fetOff);   // F5
    n.R ("F5", "BF", 100e3);              // R88
    n.opAmp ("0", "S10", "BF");           // T10
    //-- output: C23 + C24 in series into R53 --------------------------------------------------
    n.C ("O8", "out", 110e-6);
    n.R ("out", "0", 10e3);
    return n;
}

double gainDb (const Settings& s, double f, const char* node = "out")
{
    const Net n = build (s);
    return 20.0 * std::log10 (std::abs (n.solve (f)[(size_t) n.index.at (node)]));
}

struct Point { double hz, db; };

/** The highest (or lowest) point of the response between lo and hi. */
Point extreme (const Settings& s, double lo, double hi, bool lowest = false)
{
    Point best { lo, lowest ? 1e9 : -1e9 };
    for (int i = 0; i <= 400; ++i)
    {
        const double f = lo * std::pow (hi / lo, i / 400.0), g = gainDb (s, f);
        if (lowest ? g < best.db : g > best.db) best = { f, g };
    }
    for (double step = std::pow (hi / lo, 1.0 / 400.0); step > 1.0 + 1e-7; step = std::sqrt (step))
        for (double f : { best.hz / step, best.hz * step })
            if (const double g = gainDb (s, f); lowest ? g < best.db : g > best.db) best = { f, g };
    return best;
}

/** Centre over the -3 dB bandwidth (from the peak) of a boost. */
double q3 (const Settings& s, Point peak)
{
    auto edge = [&] (double dir)
    {
        double a = peak.hz, b = peak.hz;
        for (int i = 0; i < 400 && gainDb (s, b) > peak.db - 3.0; ++i) { a = b; b *= std::pow (2.0, dir / 50.0); }
        for (int i = 0; i < 40; ++i) { const double m = std::sqrt (a * b); (gainDb (s, m) > peak.db - 3.0 ? a : b) = m; }
        return std::sqrt (a * b);
    };
    const double lo = edge (-1.0), hi = edge (1.0);
    return std::sqrt (lo * hi) / (hi - lo);
}

const char* kBand[] = { "HF", "HMF", "LMF", "LF" };
const double kLo[] = { 500, 200, 40, 10 }, kHi[] = { 60000, 30000, 8000, 2000 };

/** Bell (HF, LF) or the highest Q (mid bands): the set-up reference. */
Settings reference()
{
    Settings s;
    s.bellHf = s.bellLf = true;
    return s;
}

double trim (Settings s, int band)
{
    // Less resistance, more positive feedback: more boost. Bisect for +18 dB.
    double lo = 0.0, hi = band == 0 || band == 3 ? 10e3 : 500.0;
    double* rv[] = { &s.rv1, &s.rv2, &s.rv3, &s.rv4 };
    s.gain[band] = 1.0;
    for (int i = 0; i < 60; ++i)
    {
        *rv[band] = 0.5 * (lo + hi);
        (extreme (s, kLo[band], kHi[band]).db > 18.0 ? lo : hi) = *rv[band];
    }
    return 0.5 * (lo + hi);
}

//== The filter section: HPF (T11, T12, 18 dB/oct) and LPF (T13, 12 dB/oct) =====================
// docs/research/82e242-filters.md. Pin 42 in, pin 51 between the two filters, the LPF switch's
// common contact out ("fout"); the output is left unloaded (the plug-in buffers it into the EQ).

constexpr double kHpfPotOhms = 47e3, kLpfPotOhms = 10e3;
constexpr double kHpfMidOhms = 0.15 * kHpfPotOhms;   // "mid position" of an A/log pot (the EQ set-up's convention)
constexpr double kC43 = 6.8e-9, kC43Drawn = 68e-9;   // decision 2: 6n8 (the drawing prints 68n)

struct FilterSettings
{
    bool hpfIn = true, lpfIn = true;
    double hpfOhms = kHpfMidOhms;        // each gang of the 47k A/log triple, as a rheostat
    double lpfOhms = 0.5 * kLpfPotOhms;  // each gang of the 10k lin dual, as a rheostat
    double rv5 = 288.15, rv6 = 1879.60;  // set-up trimmers (see `trims`)
    double c43 = kC43;
    double opAmpGain = 1500.0;
};

Net buildFilters (const FilterSettings& s)
{
    Net n;
    n.opAmpGain = s.opAmpGain;
    const double swOn = 0.1, swOff = 1e12;   // the switches on the pots (a closed contact, open)
    const auto rheostat = [] (double ohms) { return std::max (5.0, ohms); };
    n.source ("in");                       // pin 42
    //-- HPF, first-order section: T11 ----------------------------------------------------------
    n.R ("in", "x", std::max (0.5, s.rv5));   // RV5 500R (UNITY GAIN SET), a rheostat: wiper tied to its R56 end
    n.R ("x", "0", 300.0);                 // R56
    n.C ("x", "p11", 220e-9);              // C34 (C25 N/C)
    n.R ("p11", "0", 1e6);                 // R79
    n.R ("p11", "h7", 1.8e3);              // R55 -> pin 7
    n.R ("h7", "0", rheostat (s.hpfOhms)); // gang c
    n.opAmp ("p11", "o11", "o11");         // T11, a follower
    //-- HPF, second-order section: T12 (Sallen-Key, gain K) --------------------------------------
    n.C ("o11", "m", 220e-9);              // C36
    n.C ("m", "p12", 220e-9);              // C37
    n.R ("m", "h56", 1.8e3);               // R59 -> pin 56
    n.R ("h56", "h54", rheostat (s.hpfOhms));   // gang a -> pin 54, after R64 (the feedback)
    n.R ("p12", "h55", 1.8e3);             // R60 -> pin 55
    n.R ("h55", "0", rheostat (s.hpfOhms));     // gang b
    n.opAmp ("p12", "n12", "o12");         // T12
    n.R ("o12", "n12", 7.5e3);             // R58
    n.R ("n12", "q12", std::max (0.5, s.rv6));  // RV6 5k (LF FILTER CORNER), a rheostat
    n.R ("q12", "0", 5.6e3);               // R57
    n.C ("o12", "b12", 220e-6);            // C39
    n.R ("o12", "b12", 100e3);             // R61 (R63 1M to +18 V only polarises)
    n.C ("b12", "h65", 220e-6);            // C40
    n.R ("h65", "0", 10e3);                // R65
    n.R ("h65", "h54", 10.0);              // R64 -> pin 54
    //-- S4: pin 51 from the filter (pin 54) or the input line (pin 42) ------------------------
    n.R ("h54", "p51", s.hpfIn ? swOn : swOff);
    n.R ("in", "p51", s.hpfIn ? swOff : swOn);
    n.R ("p51", "0", 10e3);                // R66
    //-- LPF: T13 (unity-gain Sallen-Key) --------------------------------------------------------
    n.C ("p51", "b13", 220e-6);            // C41
    n.R ("b13", "0", 100e3);               // R68 (R67 1M to +18 V only polarises)
    n.C ("b13", "l70", 220e-6);            // C42
    n.R ("l70", "0", 10e3);                // R70
    n.R ("l70", "l44", 680.0);             // R69 -> pin 44
    n.R ("l44", "l45", rheostat (s.lpfOhms));   // gang a -> pin 45
    n.R ("l45", "l46", rheostat (s.lpfOhms));   // gang b -> pin 46
    n.R ("l46", "p13", 680.0);             // R71
    n.C ("p13", "0", 3.3e-9);              // C44
    n.C ("l45", "o13", s.c43);             // C43 ("HF FILTER" line)
    n.opAmp ("p13", "o13", "o13");         // T13, a follower
    n.C ("o13", "b14", 220e-6);            // C46
    n.R ("o13", "b14", 100e3);             // R72 (R74 1M to +18 V only polarises)
    n.C ("b14", "l76", 220e-6);            // C47
    n.R ("l76", "0", 10e3);                // R76
    n.R ("l76", "l47", 10.0);              // R75 -> pin 47
    //-- the LPF's switch: the output from the filter (pin 47) or pin 51 -----------------------
    n.R ("l47", "fout", s.lpfIn ? swOn : swOff);
    n.R ("p51", "fout", s.lpfIn ? swOff : swOn);
    return n;
}

double filterDb (const FilterSettings& s, double f, const char* node = "fout")
{
    const Net n = buildFilters (s);
    return 20.0 * std::log10 (std::abs (n.solve (f)[(size_t) n.index.at (node)]));
}

/** T12's closed-loop gain (its + input to its output) in the passband. */
double stageK (const FilterSettings& s)
{
    const Net n = buildFilters (s);
    const auto v = n.solve (1000.0);
    return std::abs (v[(size_t) n.index.at ("o12")] / v[(size_t) n.index.at ("p12")]);
}

/** The filters' passbands: the HPF's high above its corner, the LPF's low below its own. */
constexpr double kHpfPassHz = 10000.0, kLpfPassHz = 100.0;

/** Where the response is 3 dB under the passband (the filter's corner): bisected on log f. */
double corner3 (const FilterSettings& s, bool highPass)
{
    const double ref = filterDb (s, highPass ? kHpfPassHz : kLpfPassHz);
    double lo = highPass ? 1.0 : 300.0, hi = highPass ? 3000.0 : 400e3;
    for (int i = 0; i < 80; ++i)
    {
        const double m = std::sqrt (lo * hi);
        const bool below = filterDb (s, m) < ref - 3.0;
        (highPass ? (below ? lo : hi) : (below ? hi : lo)) = m;
    }
    return std::sqrt (lo * hi);
}

/** The largest rise above the passband (peaking), dB. */
double peaking (const FilterSettings& s, bool highPass)
{
    const double ref = filterDb (s, highPass ? kHpfPassHz : kLpfPassHz);
    double most = -1e9;
    for (int i = 0; i <= 600; ++i)
        most = std::max (most, filterDb (s, 5.0 * std::pow (40000.0 / 5.0, i / 600.0)) - ref);
    return most;
}

/** Q of a second-order section from where its phase turns by 90 degrees (H = j K Q there for a
    high-pass, -j Q for a low-pass): T12's stage (T11's output to T12's) or the whole LPF. */
double sectionQ (const FilterSettings& s, bool highPass)
{
    const auto h = [&] (double f)
    {
        const Net n = buildFilters (s);
        const auto v = n.solve (f);
        return highPass ? v[(size_t) n.index.at ("o12")] / v[(size_t) n.index.at ("o11")]
                        : v[(size_t) n.index.at ("o13")] / v[(size_t) n.index.at ("l70")];
    };
    double lo = highPass ? 2.0 : 500.0, hi = highPass ? 3000.0 : 300e3;
    // High-pass: the phase falls from +180 to 0 through +90; low-pass: from 0 to -180 through -90.
    for (int i = 0; i < 80; ++i)
    {
        const double m = std::sqrt (lo * hi), ph = std::arg (h (m)) * 180.0 / kPi;
        (highPass ? (ph > 90.0 ? lo : hi) : (ph > -90.0 ? lo : hi)) = m;
    }
    const double f90 = std::sqrt (lo * hi);
    const double pass = std::abs (h (highPass ? 20000.0 : 10.0));
    return std::abs (h (f90)) / pass;
}

const double kHpfGrid[] = { 0, 250, 500, 1e3, 1.5e3, 2e3, 3e3, 4e3, 5e3, 6e3, 7.5e3, 10e3, 12.5e3, 15e3, 20e3, 25e3, 30e3, 35e3, 40e3, 47e3 };
const double kLpfGrid[] = { 0, 100, 250, 500, 750, 1e3, 1.5e3, 2e3, 2.5e3, 3e3, 4e3, 5e3, 6e3, 7e3, 8e3, 9e3, 10e3 };
/** The module interpolates the laws on log (R + offset) against log f (the corner goes as
    about 1 / (R + leg resistor)). */
constexpr double kHpfLawOffset = 1800.0, kLpfLawOffset = 680.0;

/** The rheostat for a -3 dB point, from a law table, as the module does it. */
double lawOhms (const double* grid, const std::vector<double>& hz, double offset, double f)
{
    const int n = (int) hz.size();
    std::vector<double> lr ((size_t) n), lf ((size_t) n);
    for (int i = 0; i < n; ++i) { lr[(size_t) i] = std::log (grid[i] + offset); lf[(size_t) i] = std::log (hz[(size_t) i]); }
    const double y = std::log (f);
    const bool rising = lf[(size_t) n - 1] > lf[0];
    if (rising ? y <= lf[0] : y >= lf[0]) return grid[0];
    if (rising ? y >= lf[(size_t) n - 1] : y <= lf[(size_t) n - 1]) return grid[n - 1];
    int i = 0;
    while (i < n - 2 && (rising ? lf[(size_t) i + 1] < y : lf[(size_t) i + 1] > y)) ++i;
    const double t = (y - lf[(size_t) i]) / (lf[(size_t) i + 1] - lf[(size_t) i]);
    return std::exp (lr[(size_t) i] + t * (lr[(size_t) i + 1] - lr[(size_t) i])) - offset;
}

/** Both frequency laws from the circuit (each filter alone, its -3 dB point at every grid
    resistance), and the rheostat they give for a knob. */
struct FilterLaws
{
    std::vector<double> hpfHz, lpfHz;

    explicit FilterLaws (const FilterSettings& t)
    {
        for (double r : kHpfGrid) { FilterSettings u = t; u.lpfIn = false; u.hpfOhms = r; hpfHz.push_back (corner3 (u, true)); }
        for (double r : kLpfGrid) { FilterSettings u = t; u.hpfIn = false; u.lpfOhms = r; lpfHz.push_back (corner3 (u, false)); }
    }
    double hpfOhms (double hz) const { return lawOhms (kHpfGrid, hpfHz, kHpfLawOffset, hz); }
    double lpfOhms (double hz) const { return lawOhms (kLpfGrid, lpfHz, kLpfLawOffset, hz); }
};

/** Decision 1: RV6 for K = 2, then RV5 for 0 dB at 1 kHz with the HPF in at its mid position
    (LPF out). Returns the default settings (both filters in) with those trims. */
FilterSettings trimFilters()
{
    FilterSettings s;            // the HPF alone, at its mid position
    s.lpfIn = false;
    double lo = 0.0, hi = 5e3;   // more RV6, less gain
    for (int i = 0; i < 60; ++i) { s.rv6 = 0.5 * (lo + hi); (stageK (s) > 2.0 ? lo : hi) = s.rv6; }
    s.rv6 = 0.5 * (lo + hi);
    lo = 0.0; hi = 500.0;        // more RV5, less signal
    for (int i = 0; i < 60; ++i) { s.rv5 = 0.5 * (lo + hi); (filterDb (s, 1000.0) > 0.0 ? lo : hi) = s.rv5; }
    s.rv5 = 0.5 * (lo + hi);
    FilterSettings trimmed;   // both filters in, at their defaults
    trimmed.rv5 = s.rv5;
    trimmed.rv6 = s.rv6;
    return trimmed;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "spec";
    if (mode == "tables")
    {
        Settings s = reference();
        s.rv1 = trim (s, 0); s.rv2 = trim (s, 1); s.rv3 = trim (s, 2); s.rv4 = trim (s, 3);
        std::printf ("// trimmers: RV1 %.1f, RV2 %.2f, RV3 %.2f, RV4 %.1f\n", s.rv1, s.rv2, s.rv3, s.rv4);
        const double grid[] = { 0, 250, 500, 1e3, 1.5e3, 2e3, 3e3, 4e3, 5e3, 6e3, 7.5e3, 10e3, 12.5e3, 15e3, 20e3, 25e3, 30e3, 35e3, 40e3, 45e3, 50e3 };
        for (int b = 0; b < 4; ++b)
        {
            std::printf ("// %s centre (Hz) of a full boost against the frequency rheostat\n{ ", kBand[b]);
            for (double r : grid) { Settings t = s; t.gain[b] = 1.0; t.rheostat[b] = r; std::printf ("%.5g, ", extreme (t, kLo[b], kHi[b]).hz); }
            std::printf ("}\n");
        }
        for (int b = 0; b < 4; ++b)
        {
            Settings t = s;
            t.gain[b] = 1.0;
            const double centre = extreme (t, kLo[b], kHi[b]).hz;
            std::printf ("// %s gain (dB) at %.1f Hz against the boost / cut travel, 0 .. 1 in 40 steps (the detent is 0 dB)\n{ ", kBand[b], centre);
            for (int i = 0; i <= 40; ++i) { t.gain[b] = i / 40.0; std::printf ("%.4f, ", i == 20 ? 0.0 : gainDb (t, centre)); }
            std::printf ("}\n");
        }
        const double travel[] = { 0, 0.005, 0.01, 0.02, 0.03, 0.05, 0.075, 0.1, 0.15, 0.2, 0.3, 0.4, 0.55, 0.7, 0.85, 1.0 };
        for (int b = 1; b <= 2; ++b)
        {
            std::printf ("// %s Q (-3 dB bandwidth of a full boost) against the Q travel from ACW\n{ ", kBand[b]);
            for (double x : travel)
            {
                Settings t = s;
                t.gain[b] = 1.0;
                (b == 1 ? t.qHmf : t.qLmf) = x;
                std::printf ("%.4f, ", q3 (t, extreme (t, kLo[b], kHi[b])));
            }
            std::printf ("}\n");
        }
    }
    else if (mode == "spec")
    {
        // Each band at both ends of its frequency pot: centre, boost and cut at the reference
        // (bell / highest Q), and the shelf or the lowest Q.
        for (int b = 0; b < 4; ++b)
            for (double r : { 50e3, 7.5e3, 0.0 })
            {
                Settings t = reference();
                t.rheostat[b] = r;
                t.gain[b] = 1.0;
                const Point boost = extreme (t, kLo[b], kHi[b]);
                const double q = q3 (t, boost);
                t.gain[b] = 0.0;
                const Point cut = extreme (t, kLo[b], kHi[b], true);
                Settings w = t;
                if (b == 0) w.bellHf = false;
                if (b == 3) w.bellLf = false;
                if (b == 1) w.qHmf = 1.0;
                if (b == 2) w.qLmf = 1.0;
                w.gain[b] = 1.0;
                const double far = b == 0 ? gainDb (w, 16000.0) : (b == 3 ? gainDb (w, 20.0) : extreme (w, kLo[b], kHi[b]).db);
                const double wideQ = b == 1 || b == 2 ? q3 (w, extreme (w, kLo[b], kHi[b])) : 0.0;
                std::printf ("%-3s rheostat %5.0f: centre %7.1f Hz, %+6.2f / %+6.2f dB, Q %.2f | %s %+6.2f dB",
                             kBand[b], r, boost.hz, boost.db, cut.db, q, b == 0 || b == 3 ? "full shelf" : "lowest Q", far);
                if (wideQ > 0.0) std::printf (", Q %.2f", wideQ);
                std::printf ("\n");
            }
        std::printf ("flat: %+.3f dB at 1 kHz, %+.3f dB at 20 Hz, %+.3f dB at 20 kHz\n", gainDb (Settings(), 1000.0), gainDb (Settings(), 20.0), gainDb (Settings(), 20000.0));
    }
    else if (mode == "shelves")
    {
        for (int b : { 0, 3 })
            for (double r : { 0.0, 7.5e3, 20e3, 50e3 })
                for (double g : { 1.0, 0.0 })
                {
                    Settings t = reference();
                    t.rheostat[b] = r;
                    t.gain[b] = 1.0;
                    const double centre = extreme (t, kLo[b], kHi[b]).hz;
                    (b == 0 ? t.bellHf : t.bellLf) = false;
                    t.gain[b] = g;
                    const double farHz = b == 0 ? 15000.0 : 15.0, far = gainDb (t, farHz), atCentre = gainDb (t, centre);
                    double f = farHz;
                    for (int i = 0; i < 2000 && std::abs (gainDb (t, f)) > 0.5 * std::abs (far); ++i) f *= std::pow (2.0, (b == 0 ? -1.0 : 1.0) / 96.0);
                    std::printf ("%s shelf %s, rheostat %5.0f (bell centre %7.1f Hz): %+6.2f dB at %.0f Hz, %+6.2f dB at the centre, half way at %7.1f Hz (%.2f octaves)\n",
                                 kBand[b], g > 0.5 ? "boost" : "cut  ", r, centre, far, farHz, atCentre, f, std::abs (std::log2 (f / centre)));
                }
    }
    else if (mode == "headroom")
    {
        const char* outs[] = { "O3", "O1", "BH", "mbp", "BM", "O8", "lbp", "BL", "O9", "BF", "out" };
        struct Case { const char* name; std::function<void (Settings&)> set; };
        const std::vector<Case> cases = {
            { "flat", [] (Settings&) {} },
            { "HF +18 bell", [] (Settings& t) { t.gain[0] = 1; t.bellHf = true; } },
            { "HF +15 shelf", [] (Settings& t) { t.gain[0] = 1; } },
            { "HMF +18 highest Q", [] (Settings& t) { t.gain[1] = 1; } },
            { "HMF -18 highest Q", [] (Settings& t) { t.gain[1] = 0; } },
            { "LMF +18 highest Q", [] (Settings& t) { t.gain[2] = 1; } },
            { "LF +18 bell", [] (Settings& t) { t.gain[3] = 1; t.bellLf = true; } },
            { "LF +16 shelf", [] (Settings& t) { t.gain[3] = 1; } },
            { "all full boost", [] (Settings& t) { t.gain[0] = t.gain[1] = t.gain[2] = t.gain[3] = 1; } },
            { "all full cut", [] (Settings& t) { t.gain[0] = t.gain[1] = t.gain[2] = t.gain[3] = 0; } } };
        std::printf ("largest gain (dB) from the input, 10 Hz - 40 kHz:\n%-18s", "");
        for (const char* o : outs) std::printf (" %6s", o);
        std::printf ("\n");
        for (const auto& c : cases)
        {
            Settings t;
            c.set (t);
            const Net n = build (t);
            std::vector<double> most (std::size (outs), 0.0);
            for (int i = 0; i <= 240; ++i)
            {
                const auto v = n.solve (10.0 * std::pow (4000.0, i / 240.0));
                for (size_t k = 0; k < most.size(); ++k) most[k] = std::max (most[k], std::abs (v[(size_t) n.index.at (outs[k])]));
            }
            std::printf ("%-18s", c.name);
            for (double m : most) std::printf (" %+6.1f", 20.0 * std::log10 (m + 1e-12));
            std::printf ("\n");
        }
    }
    else if (mode == "lfq")
    {
        // The ECO trims RV4 for +18 dB with the bell in and the frequency pot at mid travel, and
        // specifies Q 1.7 +-10 % and +-18 +-1 dB. What each target leaves of the other.
        const auto lfBoost = [] (Settings t) { t.gain[3] = 1.0; return t; };
        const auto lfQ = [] (const Settings& t) { return q3 (t, extreme (t, kLo[3], kHi[3])); };
        /** RV4 (bisected) for which `measure` reaches `target`; more RV4, less feedback, less of both. */
        const auto trimFor = [] (Settings t, const std::function<double (const Settings&)>& measure, double target)
        {
            double lo = 0.0, hi = 10e3;
            for (int i = 0; i < 50; ++i) { t.rv4 = 0.5 * (lo + hi); (measure (t) > target ? lo : hi) = t.rv4; }
            t.rv4 = 0.5 * (lo + hi);
            return t;
        };
        const auto peakDb = [] (const Settings& t) { return extreme (t, kLo[3], kHi[3]).db; };
        for (double a : { 1500.0, 1e5 })
        {
            Settings ref = lfBoost (reference());
            ref.opAmpGain = a;
            std::printf ("op-amp gain %.0f:\n", a);
            Settings t = trimFor (ref, peakDb, 18.0);
            std::printf ("  trimmed for +18 dB (the ECO): RV4 %6.1f, Q %.2f\n", t.rv4, lfQ (t));
            for (double q : { 1.7, 1.87 })
            {
                t = trimFor (ref, lfQ, q);
                std::printf ("  trimmed for Q %.2f: RV4 %6.1f, %+6.2f dB\n", q, t.rv4, peakDb (t));
            }
            t = trimFor (ref, peakDb, 17.0);
            std::printf ("  trimmed for +17 dB (the tolerance's edge): RV4 %6.1f, Q %.2f\n", t.rv4, lfQ (t));
        }
        for (double r : { 7.5e3, 10e3, 11e3, 12e3, 33e3 })
        {
            Settings ref = lfBoost (reference());
            ref.r87 = r;
            const Settings t = trimFor (ref, peakDb, 18.0);
            std::printf ("R87 %5.0f: +18 dB at RV4 %6.1f gives Q %.2f\n", r, t.rv4, lfQ (t));
        }
    }
    else if (mode == "curve" && argc > 5)
    {
        Settings s;
        const int b = std::atoi (argv[2]);
        s.gain[b] = std::atof (argv[3]);
        s.rheostat[b] = std::atof (argv[4]);
        const double shape = std::atof (argv[5]);
        if (b == 0) s.bellHf = shape > 0.5;
        if (b == 3) s.bellLf = shape > 0.5;
        if (b == 1) s.qHmf = shape;
        if (b == 2) s.qLmf = shape;
        for (int i = 0; i <= 60; ++i)
        {
            const double f = 10.0 * std::pow (2000.0, i / 60.0);
            std::printf ("%9.1f %+7.3f\n", f, gainDb (s, f));
        }
    }
    else if (mode == "trims")
    {
        const FilterSettings t = trimFilters();
        std::printf ("RV6 (LF FILTER CORNER) for K = 2: %.2f ohms (ideal op-amp: 1900); T12 stage gain %.5f, its Q %.4f\n", t.rv6, stageK (t), sectionQ (t, true));
        std::printf ("RV5 (UNITY GAIN SET) for 0 dB at 1 kHz, HPF at mid position (gangs %.0f ohms): %.2f ohms; divider R56 / (R56 + RV5) = %.4f, source %.1f ohms\n",
                     kHpfMidOhms, t.rv5, 300.0 / (300.0 + t.rv5), 300.0 * t.rv5 / (300.0 + t.rv5));
        for (double r : { 0.0, kHpfMidOhms, kHpfPotOhms })
        {
            FilterSettings u = t;
            u.lpfIn = false;
            u.hpfOhms = r;
            std::printf ("  gangs %5.0f: passband %+.3f dB at 1 kHz, %+.3f dB at 10 kHz; peaking %+.3f dB; -3 dB at %.2f Hz; T12 stage Q %.4f\n",
                         r, filterDb (u, 1000.0), filterDb (u, 10000.0), peaking (u, true), corner3 (u, true), sectionQ (u, true));
        }
        std::printf ("// Filter set-up trimmers (decision 1: maximally flat, unity gain), from tools/ssl_eq_ac.cpp trims.\n");
        std::printf ("constexpr Real kRv5 = %.2f, kRv6 = %.2f;\n", t.rv5, t.rv6);
    }
    else if (mode == "laws")
    {
        const FilterSettings t = trimFilters();
        const FilterLaws laws (t);
        std::printf ("// HPF: each gang of the 47k rheostat (ohms) -> -3 dB point (Hz, against its passband)\nconstexpr double kHpfOhms[] = { ");
        for (double r : kHpfGrid) std::printf ("%.0f, ", r);
        std::printf ("};\nconstexpr double kHpfHz[] = { ");
        for (double f : laws.hpfHz) std::printf ("%.5g, ", f);
        std::printf ("};\n// LPF: each gang of the 10k rheostat (ohms) -> -3 dB point (Hz, against its passband), C43 6n8\nconstexpr double kLpfOhms[] = { ");
        for (double r : kLpfGrid) std::printf ("%.0f, ", r);
        std::printf ("};\nconstexpr double kLpfHz[] = { ");
        for (double f : laws.lpfHz) std::printf ("%.5g, ", f);
        std::printf ("};\n");
        // The module's interpolation (log (R + offset) against log f) against the circuit.
        for (int side = 0; side < 2; ++side)
        {
            const bool hp = side == 0;
            const std::vector<double>& law = hp ? laws.hpfHz : laws.lpfHz;   // highest corner first
            const double lo = hp ? 15.0 : 3000.0, hi = hp ? 400.0 : 22000.0;
            double worst = 0.0, worstAt = 0.0;
            for (int i = 0; i <= 48; ++i)
            {
                const double f = lo * std::pow (hi / lo, i / 48.0);
                FilterSettings u = t;
                (hp ? u.lpfIn : u.hpfIn) = false;
                (hp ? u.hpfOhms : u.lpfOhms) = hp ? laws.hpfOhms (f) : laws.lpfOhms (f);
                const double got = corner3 (u, hp);
                const bool reachable = f >= law.back() && f <= law.front();
                if (reachable && std::abs (got / f - 1.0) > worst) { worst = std::abs (got / f - 1.0); worstAt = f; }
            }
            std::printf ("%s law, interpolated as the module does: -3 dB within %.3f %% of the knob (worst at %.1f Hz) where the pots reach\n",
                         hp ? "HPF" : "LPF", 100.0 * worst, worstAt);
        }
        std::printf ("ranges: HPF %.2f Hz (gangs at 47k) to %.2f Hz (at 0); LPF %.1f Hz (gangs at 10k) to %.0f Hz (at 0)\n",
                     laws.hpfHz.back(), laws.hpfHz.front(), laws.lpfHz.back(), laws.lpfHz.front());
    }
    else if (mode == "filters")
    {
        const FilterSettings t = trimFilters();
        const FilterLaws laws (t);
        std::printf ("HPF alone (LPF out), gangs from the law:\n  knob Hz   gangs   -3 dB Hz  passband(10k)  peaking  slope fc/4..fc/2  at 20 Hz  T12 Q\n");
        for (double k : { 15.0, 30.0, 80.0, 200.0, 400.0 })
        {
            FilterSettings u = t;
            u.lpfIn = false;
            u.hpfOhms = laws.hpfOhms (k);
            const double fc = corner3 (u, true);
            std::printf ("  %7.1f %7.0f %10.2f %+12.3f %+9.3f %12.2f dB/oct %+9.2f %7.3f\n", k, u.hpfOhms, fc, filterDb (u, kHpfPassHz), peaking (u, true),
                         filterDb (u, fc / 2.0) - filterDb (u, fc / 4.0), filterDb (u, 20.0), sectionQ (u, true));
        }
        std::printf ("LPF alone (HPF out), gangs from the law, C43 6n8:\n  knob Hz   gangs   -3 dB Hz  passband(100)  peaking  slope 2fc..4fc   at 20 kHz  Q\n");
        for (double k : { 3000.0, 3200.0, 6000.0, 12000.0, 20000.0, 22000.0 })
        {
            FilterSettings u = t;
            u.hpfIn = false;
            u.lpfOhms = laws.lpfOhms (k);
            const double fc = corner3 (u, false);
            std::printf ("  %7.0f %7.0f %10.1f %+12.3f %+9.3f %12.2f dB/oct %+9.2f %7.3f\n", k, u.lpfOhms, fc, filterDb (u, kLpfPassHz), peaking (u, false),
                         filterDb (u, 2.0 * fc) - filterDb (u, 4.0 * fc), filterDb (u, 20000.0), sectionQ (u, false));
        }
        // Headroom: the largest gain from the input to each op-amp output (the HPF's internal
        // nodes, T13), 10 Hz - 40 kHz.
        std::printf ("largest gain (dB) from the input, 10 Hz - 40 kHz:      T11     T12     T13    out\n");
        struct Case { const char* name; double hpfOhms, lpfOhms; };
        const Case cases[] = { { "HPF 400 Hz + LPF 3.2 kHz", 0.0, 10e3 },
                               { "HPF 80 Hz + LPF 12 kHz", laws.hpfOhms (80.0), laws.lpfOhms (12000.0) },
                               { "HPF 15 Hz + LPF 22 kHz", 47e3, laws.lpfOhms (22000.0) } };
        for (const auto& c : cases)
        {
            FilterSettings u = t;
            u.hpfOhms = c.hpfOhms;
            u.lpfOhms = c.lpfOhms;
            const Net n = buildFilters (u);
            double most[4] = { 0, 0, 0, 0 };
            const char* nodes[4] = { "o11", "o12", "o13", "fout" };
            for (int i = 0; i <= 240; ++i)
            {
                const auto v = n.solve (10.0 * std::pow (4000.0, i / 240.0));
                for (int k = 0; k < 4; ++k) most[k] = std::max (most[k], std::abs (v[(size_t) n.index.at (nodes[k])]));
            }
            std::printf ("  %-48s %+6.2f  %+6.2f  %+6.2f  %+6.2f\n", c.name, 20.0 * std::log10 (most[0]), 20.0 * std::log10 (most[1]), 20.0 * std::log10 (most[2]), 20.0 * std::log10 (most[3]));
        }
        // The whole card flat: the filter section into the EQ (every gain at its detent) through
        // an ideal buffer, so the responses multiply.
        FilterSettings out = t, in = t;
        out.hpfIn = out.lpfIn = false;
        in.hpfOhms = laws.hpfOhms (80.0);
        in.lpfOhms = laws.lpfOhms (12000.0);
        std::printf ("the whole card, EQ flat (dB):\n      Hz  filters out  HPF 80 + LPF 12k   EQ alone\n");
        for (double f : { 10.0, 20.0, 40.0, 80.0, 160.0, 1000.0, 5000.0, 10000.0, 12000.0, 16000.0, 20000.0 })
        {
            const Settings eq;
            const double e = gainDb (eq, f);
            std::printf ("  %6.0f %+12.3f %+18.3f %+10.3f\n", f, filterDb (out, f) + e, filterDb (in, f) + e, e);
        }
    }
    else if (mode == "lpf68n")
    {
        // Decision 2: C43 as the drawing prints it (68n) against the 6n8 the model uses.
        const FilterSettings t = trimFilters();
        std::printf ("LPF alone (HPF out):  gangs  C43   -3 dB Hz   f0 (-90 deg)   Q     peak dB at Hz\n");
        for (double r : { 0.0, 850.0, 2500.0, 5000.0, 10e3 })
            for (double c : { kC43, kC43Drawn })
            {
                FilterSettings u = t;
                u.hpfIn = false;
                u.lpfOhms = r;
                u.c43 = c;
                double lo = 300.0, hi = 300e3;
                for (int i = 0; i < 80; ++i)
                {
                    const double m = std::sqrt (lo * hi);
                    const Net n = buildFilters (u);
                    const auto v = n.solve (m);
                    (std::arg (v[(size_t) n.index.at ("o13")] / v[(size_t) n.index.at ("l70")]) > -kPi / 2.0 ? lo : hi) = m;
                }
                double peakHz = 0.0, peakDb = -1e9;
                const double ref = filterDb (u, kLpfPassHz);
                for (int i = 0; i <= 600; ++i)
                {
                    const double f = 300.0 * std::pow (1000.0, i / 600.0);
                    if (const double g = filterDb (u, f) - ref; g > peakDb) { peakDb = g; peakHz = f; }
                }
                std::printf ("                      %5.0f  %-4s %9.1f %12.1f %8.3f %+7.2f at %.0f\n", r, c == kC43 ? "6n8" : "68n", corner3 (u, false), std::sqrt (lo * hi),
                             sectionQ (u, false), peakDb, peakHz);
            }
        std::printf ("the LPF at gangs 5k (dB):\n      Hz      6n8      68n\n");
        for (double f : { 500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0, 4000.0, 5000.0, 6000.0, 8000.0, 10000.0, 15000.0, 20000.0 })
        {
            FilterSettings a = t, b = t;
            a.hpfIn = b.hpfIn = false;
            a.lpfOhms = b.lpfOhms = 5e3;
            b.c43 = kC43Drawn;
            std::printf ("  %6.0f %+8.2f %+8.2f\n", f, filterDb (a, f), filterDb (b, f));
        }
    }
    else if (mode == "fcurve" && argc > 5)
    {
        FilterSettings s = trimFilters();
        s.hpfIn = std::atoi (argv[2]) != 0;
        s.hpfOhms = std::atof (argv[3]);
        s.lpfIn = std::atoi (argv[4]) != 0;
        s.lpfOhms = std::atof (argv[5]);
        if (argc > 6) s.c43 = std::atof (argv[6]);
        for (int i = 0; i <= 72; ++i)
        {
            const double f = 10.0 * std::pow (4000.0, i / 72.0);
            std::printf ("%9.1f %+8.3f\n", f, filterDb (s, f));
        }
    }
    else
    {
        std::printf ("usage: classic_channel_eq_ac tables | spec | shelves | headroom | lfq | curve band gain rheostat shape\n"
                     "       classic_channel_eq_ac trims | laws | filters | lpf68n | fcurve hpf-in hpf-ohms lpf-in lpf-ohms [c43]\n");
        return 1;
    }
    return 0;
}
