// Offline regression suite for the SSL 4000 E card model (Source/dsp/SslE242.*), no JUCE. The EQ
// band checks are ported from the analog-recreation project's fx_test (its SSL section) and run
// at 44.1, 48 and 96 kHz; the filter checks are this plug-in's. The model is a circuit, so the
// checks are against SSL's Rev. 4 specification and against the schematic's own AC analysis
// (tools/ssl_eq_ac.cpp), not against exact formulas. Prints measured against expected for every
// check and exits non-zero on a failure.
//
//   classic_channel_test            everything
//   classic_channel_test --quick    the filter and switch checks only

#include "dsp/SslE242.h"
#include "dsp/SslE10Dynamics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ar;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kRates[] = { 44100.0, 48000.0, 96000.0 };
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    ++checks;
    if (! ok)
        ++failures;
}

std::string fmt (const char* format, double a, double b = 0.0, double c = 0.0, double d = 0.0, double e = 0.0, double f = 0.0)
{
    char text[512];
    std::snprintf (text, sizeof (text), format, a, b, c, d, e, f);
    return text;
}

double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-30)); }

/** Runs fn (i) for i in [0, n) on all cores. fn must not print or touch shared state. */
void parallelFor (int n, const std::function<void (int)>& fn)
{
    std::atomic<int> next { 0 };
    std::vector<std::thread> pool;
    const int workers = std::max (1, std::min (n, (int) std::thread::hardware_concurrency()));
    for (int w = 0; w < workers; ++w)
        pool.emplace_back ([&] { for (int i; (i = next++) < n;) fn (i); });
    for (auto& t : pool)
        t.join();
}

using Settings = std::vector<std::pair<std::string, float>>;

/** The module (or one of its sections) at fs with every control at its default but `sets`. */
ModulePtr makeSsl (double fs, const Settings& sets, fx::SslSections sections = fx::SslSections::all)
{
    auto m = fx::makeSslE242 (sections);
    m->prepare (fs);
    const auto specs = m->params();
    for (size_t i = 0; i < specs.size(); ++i)
    {
        float v = specs[i].def;
        for (const auto& s : sets)
            if (specs[i].id == s.first)
                v = s.second;
        m->setParam ((int) i, v);
    }
    m->reset();
    return m;
}

void runMono (Module& m, std::vector<float>& x, int block = 512)
{
    for (size_t s = 0; s < x.size(); s += (size_t) block)
        m.process (x.data() + s, (int) std::min<size_t> ((size_t) block, x.size() - s));
}

/** Impulse response at a level where the op-amps are linear, per unit input. The default length
    (2.7 s) keeps the coupling capacitors' tails from showing above 20 Hz. */
std::vector<float> impulse (Module& m, double fs, size_t length = 0)
{
    if (length == 0)
        length = fs > 60000.0 ? (1u << 18) : (1u << 17);
    std::vector<float> h (length, 0.0f);
    h[0] = 0.01f;
    runMono (m, h);
    for (auto& v : h) v /= 0.01f;
    return h;
}

std::vector<float> impulseOf (double fs, const Settings& sets, fx::SslSections sections = fx::SslSections::all, size_t length = 0)
{
    auto m = makeSsl (fs, sets, sections);
    return impulse (*m, fs, length);
}

/** H(f) of an impulse response (a direct DFT at f). */
std::complex<double> response (const std::vector<float>& h, double hz, double fs)
{
    const std::complex<double> step = std::polar (1.0, -2.0 * kPi * hz / fs);
    std::complex<double> phasor (1.0, 0.0), sum (0.0, 0.0);
    for (size_t n = 0; n < h.size(); ++n)
    {
        sum += (double) h[n] * phasor;
        phasor *= step;
        if ((n & 4095) == 0)
            phasor /= std::abs (phasor);
    }
    return sum;
}

double responseDb (const std::vector<float>& h, double hz, double fs) { return db (std::abs (response (h, hz, fs))); }

/** |a / b| in dB at f: a filter's own response, the EQ (b, the same settings with the filters
    out) divided out. Exact here: the section buffer makes the card's response the product. */
double ratioDb (const std::vector<float>& a, const std::vector<float>& b, double hz, double fs)
{
    return db (std::abs (response (a, hz, fs) / response (b, hz, fs)));
}

struct Extreme { double hz, db; };

/** The largest (or, with sign -1, deepest) point of the response between lo and hi. */
Extreme extreme (const std::vector<float>& h, double fs, double lo, double hi, double sign = 1.0)
{
    Extreme best { lo, -1e9 };
    for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 48.0))
        if (const double v = sign * responseDb (h, f, fs); v > best.db) best = { f, v };
    double step = std::pow (2.0, 1.0 / 96.0);
    for (int i = 0; i < 24; ++i, step = std::sqrt (step))
        for (double f : { best.hz / step, best.hz * step })
            if (const double v = sign * responseDb (h, f, fs); v > best.db) best = { f, v };
    best.db *= sign;
    return best;
}

/** Centre over the -3 dB bandwidth (from the peak) of a boost. */
double bandwidthQ (const std::vector<float>& h, double fs, const Extreme& peak)
{
    auto edge = [&] (double dir)
    {
        double a = peak.hz, b = peak.hz;
        while (responseDb (h, b, fs) > peak.db - 3.0 && b > 5.0 && b < 0.49 * fs) { a = b; b *= std::pow (2.0, dir / 24.0); }
        for (int i = 0; i < 30; ++i) { const double m = std::sqrt (a * b); (responseDb (h, m, fs) > peak.db - 3.0 ? a : b) = m; }
        return std::sqrt (a * b);
    };
    const double lo = edge (-1.0), hi = edge (1.0);
    return std::sqrt (lo * hi) / (hi - lo);
}

//==============================================================================
void testControls()
{
    std::printf ("Controls\n");
    auto m = makeSsl (48000.0, {});
    const auto p = m->params();
    bool layout = p.size() == fx::kSslParamIds.size();
    for (size_t i = 0; layout && i < p.size(); ++i)
        layout = p[i].id == fx::kSslParamIds[i] && ! p[i].help.empty();
    check (layout, "17 controls: HPF in / freq, LPF in / freq, the bands in console order (HF, HMF, LMF, LF: gain, frequency, shape or Q), EQ in; each with help text");
    if (! layout)
        return;
    const auto find = [&p] (const char* id) -> const ParamSpec& { for (const auto& s : p) if (s.id == id) return s; return p[0]; };
    bool units = true;
    for (const char* id : { "hf_gain", "hmf_gain", "lmf_gain", "lf_gain" })
        units = units && find (id).unit == "dB" && find (id).min == -18.0f && find (id).max == 18.0f && find (id).def == 0.0f;
    for (const char* id : { "hpf_freq", "lpf_freq", "hf_freq", "hmf_freq", "lmf_freq", "lf_freq" })
        units = units && find (id).unit == "Hz" && find (id).logScale;
    for (const char* id : { "hmf_q", "lmf_q" })
        units = units && find (id).logScale && find (id).min == 0.5f;
    for (const char* id : { "hf_bell", "lf_bell" })
        units = units && find (id).type == ParamSpec::Type::choice && find (id).choices == std::vector<std::string> { "Shelf", "Bell" } && find (id).def == 0.0f;
    for (const char* id : { "hpf_on", "lpf_on", "eq_on" })
        units = units && find (id).type == ParamSpec::Type::choice && find (id).choices == std::vector<std::string> { "Out", "In" };
    units = units && find ("hpf_on").def == 0.0f && find ("lpf_on").def == 0.0f && find ("eq_on").def == 1.0f;
    check (units, "gains -18..+18 dB (0 default), log frequency knobs in Hz, log Q knobs from 0.5, Shelf / Bell, Out / In (HPF, LPF out; EQ in)");
    const auto range = [&] (const char* id, float lo, float hi, float def)
    { return std::abs (find (id).min - lo) < 0.5f && std::abs (find (id).max - hi) < 0.5f && std::abs (find (id).def - def) < 0.5f; };
    check (range ("hpf_freq", 16.0f, 390.0f, 80.0f) && range ("lpf_freq", 3200.0f, 22000.0f, 12000.0f),
           "filter knobs: HPF 16-390 Hz (default 80), LPF 3.2-22 kHz (default 12 k), -3 dB points, inside the pots' ends");
    check (range ("hf_freq", 1300.0f, 19000.0f, 8000.0f) && range ("hmf_freq", 640.0f, 9400.0f, 2500.0f) && range ("lmf_freq", 140.0f, 2000.0f, 500.0f)
               && range ("lf_freq", 37.0f, 420.0f, 100.0f) && range ("hmf_q", 0.5f, 4.1f, 1.5f) && range ("lmf_q", 0.5f, 4.2f, 1.5f),
           "band ranges are the circuit's: HF 1.3-19 kHz, HMF 640 Hz-9.4 kHz, LMF 140 Hz-2 kHz, LF 37-420 Hz (SSL: 1.5-20k, 0.6-10k, 160-2.2k, 40-450, +-10 %)");
    check (m->fullScaleInput() && m->outputVoltsForFullScale() == 1.0f && m->latencySamples() == 0, "full-scale signal in and out, no latency");
    const auto eqOnly = makeSsl (48000.0, {}, fx::SslSections::eqOnly)->params();
    const auto filtersOnly = makeSsl (48000.0, {}, fx::SslSections::filtersOnly)->params();
    bool sections = eqOnly.size() == 12 && filtersOnly.size() == 4;
    for (size_t i = 0; sections && i < 12; ++i) sections = eqOnly[i].id == p[i + 4].id;
    for (size_t i = 0; sections && i < 4; ++i) sections = filtersOnly[i].id == p[i].id;
    check (sections, "the sections for tests: EQ alone (the analog-recreation module's 12 controls), filters alone (4)");
}

void testFlat()
{
    std::printf ("Flat (every gain at its centre detent, filters out)\n");
    struct Row { double worst, at1k, at20k; };
    std::vector<Row> rows (3);
    parallelFor (3, [&] (int r)
    {
        const double fs = kRates[r];
        const auto h = impulseOf (fs, {});
        Row row { 0.0, responseDb (h, 1000.0, fs), responseDb (h, 20000.0, fs) };
        for (double f = 20.0; f <= 10000.0; f *= std::pow (2.0, 1.0 / 6.0))
            row.worst = std::max (row.worst, std::abs (responseDb (h, f, fs)));
        rows[(size_t) r] = row;
    });
    for (int r = 0; r < 3; ++r)
    {
        const auto& row = rows[(size_t) r];
        // The schematic: -0.04 dB at 1 kHz, -0.10 dB at 20 kHz (the summing amps' 220 pF,
        // pre-warped at 20 kHz).
        check (row.worst < 0.1 && std::abs (row.at1k) < 0.06 && std::abs (row.at20k + 0.10) < 0.1,
               fmt ("%.1f kHz: within %.3f dB from 20 Hz to 10 kHz (< 0.1), %+.3f dB at 1 kHz (schematic -0.04), %+.2f dB at 20 kHz (schematic -0.10)",
                    kRates[r] / 1000.0, row.worst, row.at1k, row.at20k));
    }
    // Flat does not depend on where the frequency, Q and shape controls are.
    const std::vector<Settings> ends { { { "hf_bell", 1.0f }, { "lf_bell", 1.0f } },
                                       { { "hf_freq", 1300.0f }, { "hmf_freq", 9400.0f }, { "lmf_freq", 140.0f }, { "lf_freq", 420.0f }, { "hmf_q", 4.1f }, { "lmf_q", 0.5f } },
                                       { { "hf_freq", 19000.0f }, { "hmf_freq", 640.0f }, { "lmf_freq", 2000.0f }, { "lf_freq", 37.0f }, { "hmf_q", 0.5f }, { "lmf_q", 4.2f }, { "hf_bell", 1.0f } } };
    std::vector<double> worst (ends.size());
    parallelFor ((int) ends.size(), [&] (int i)
    {
        const auto h = impulseOf (96000.0, ends[(size_t) i]);
        for (double f = 20.0; f <= 10000.0; f *= std::pow (2.0, 1.0 / 6.0))
            worst[(size_t) i] = std::max (worst[(size_t) i], std::abs (responseDb (h, f, 96000.0)));
    });
    check (*std::max_element (worst.begin(), worst.end()) < 0.15,
           fmt ("flat with the other controls at their ends (96 kHz): within %.3f dB from 20 Hz to 10 kHz (< 0.15)", *std::max_element (worst.begin(), worst.end())));
}

void testBands()
{
    std::printf ("Bands against SSL's Rev. 4 specification (filters out)\n");
    // Each band's capacitors are pre-warped at the knob, so the centre lands there at every rate
    // over the pots' whole range (the tops of HF and HMF too).
    struct Band { const char* name; std::string gain, freq, shape; std::vector<float> hz; float shapeValue; };
    const std::vector<Band> bands = {
        { "HF bell",  "hf_gain",  "hf_freq",  "hf_bell", { 1500.0f, 4000.0f, 8000.0f, 12000.0f, 19000.0f }, 1.0f },
        { "HMF",      "hmf_gain", "hmf_freq", "hmf_q",   { 700.0f, 2000.0f, 5000.0f, 9400.0f }, 4.1f },
        { "LMF",      "lmf_gain", "lmf_freq", "lmf_q",   { 150.0f, 500.0f, 1200.0f, 2000.0f }, 4.2f },
        { "LF bell",  "lf_gain",  "lf_freq",  "lf_bell", { 40.0f, 100.0f, 250.0f, 410.0f }, 1.0f } };
    struct Case { double fs; const Band* band; float hz, gain; double errHz = 0, errDb = 0; };
    std::vector<Case> cases;
    for (double fs : kRates)
        for (const auto& b : bands)
            for (float hz : b.hz)
                for (float gain : { 18.0f, -18.0f })
                    cases.push_back ({ fs, &b, hz, gain });
    parallelFor ((int) cases.size(), [&] (int i)
    {
        auto& c = cases[(size_t) i];
        const auto h = impulseOf (c.fs, { { c.band->gain, c.gain }, { c.band->freq, c.hz }, { c.band->shape, c.band->shapeValue } });
        const auto e = extreme (h, c.fs, c.hz / 1.5, std::min (c.hz * 1.5, 0.48 * c.fs), c.gain > 0.0f ? 1.0 : -1.0);
        c.errHz = std::abs (e.hz / c.hz - 1.0);
        c.errDb = std::abs (std::abs (e.db) - 18.0);
    });
    for (double fs : kRates)
    {
        double worstHz = 0.0, worstBoost = 0.0, worstCut = 0.0;
        for (const auto& c : cases)
            if (c.fs == fs)
            {
                worstHz = std::max (worstHz, c.errHz);
                (c.gain > 0.0f ? worstBoost : worstCut) = std::max (c.gain > 0.0f ? worstBoost : worstCut, c.errDb);
            }
        // The bands of a section reach each other's wipers through 12k (R94-R97), so where the
        // neighbouring band sits moves a centre by a few percent.
        check (worstHz < 0.03, fmt ("%.1f kHz: full boost / cut, bell or highest Q, HF to 19 kHz and HMF to 9.4 kHz: centre within %.2f %% of the knob (< 3 %%; SSL +-10 %%)", fs / 1000.0, 100.0 * worstHz));
        check (worstBoost < 0.6 && worstCut < 0.6, fmt ("%.1f kHz: +-18 dB at the highest Q / bell: boosts within %.2f dB, cuts within %.2f dB (< 0.6; SSL +-1 dB)", fs / 1000.0, worstBoost, worstCut));
    }

    // Q: the -3 dB bandwidth of a full boost, 0.5 to about 4 (SSL: 0.5-4.5 +-15 %).
    struct QCase { std::string band; float hz, q; double err = 0, peak = 0; };
    std::vector<QCase> qs;
    for (const auto& band : { std::make_pair (std::string ("hmf"), 3000.0f), std::make_pair (std::string ("lmf"), 600.0f) })
        for (float q : { 0.5f, 1.0f, 2.0f, 4.0f })
            qs.push_back ({ band.first, band.second, q });
    parallelFor ((int) qs.size(), [&] (int i)
    {
        auto& c = qs[(size_t) i];
        const auto h = impulseOf (96000.0, { { c.band + "_gain", 18.0f }, { c.band + "_freq", c.hz }, { c.band + "_q", c.q } });
        const auto e = extreme (h, 96000.0, c.hz / 1.5, c.hz * 1.5);
        c.err = std::abs (bandwidthQ (h, 96000.0, e) / c.q - 1.0);
        c.peak = e.db;
    });
    double worstQ = 0.0, lowQRange = 0.0;
    for (const auto& c : qs)
    {
        worstQ = std::max (worstQ, c.err);
        if (c.q == 0.5f) lowQRange = std::max (lowQRange, std::abs (c.peak - 15.5));
    }
    check (worstQ < 0.1, fmt ("HMF / LMF Q 0.5, 1, 2, 4 at full boost (96 kHz): -3 dB bandwidths within %.1f %% of the knob (< 10 %%)", 100.0 * worstQ));
    check (lowQRange < 1.0, fmt ("the widest Q tops out near +15.5 dB (SSL: +-15 +-1 dB at the lowest Q): within %.2f dB", lowQRange));

    // Bell Q of HF (SSL: 1.0 +-10 %), and the 6 dB/oct shelves (SSL: +-15 +-1 dB).
    std::vector<std::vector<float>> irs (5);
    const std::vector<Settings> shelfSets { { { "hf_gain", 18.0f }, { "hf_freq", 4000.0f }, { "hf_bell", 1.0f } },
                                            { { "hf_gain", 18.0f }, { "hf_freq", 6000.0f } }, { { "lf_gain", 18.0f }, { "lf_freq", 100.0f } },
                                            { { "hf_gain", -18.0f }, { "hf_freq", 6000.0f } }, { { "lf_gain", -18.0f }, { "lf_freq", 100.0f } } };
    parallelFor (5, [&] (int i) { irs[(size_t) i] = impulseOf (96000.0, shelfSets[(size_t) i]); });
    const double q = bandwidthQ (irs[0], 96000.0, extreme (irs[0], 96000.0, 2600.0, 6000.0));
    check (q > 0.85 && q < 1.15, fmt ("HF bell at 4 kHz: Q %.2f (SSL: 1.0 +-10 %%; schematic 0.85-0.97)", q));
    double worst = 0.0;
    for (int g = 0; g < 2; ++g)
    {
        const auto& hh = irs[(size_t) (1 + 2 * g)];
        const auto& hl = irs[(size_t) (2 + 2 * g)];
        const double hfTop = std::abs (responseDb (hh, 20000.0, 96000.0)), hfLow = std::abs (responseDb (hh, 100.0, 96000.0));
        const double lfBottom = std::abs (responseDb (hl, 20.0, 96000.0)), lfHigh = std::abs (responseDb (hl, 10000.0, 96000.0));
        worst = std::max ({ worst, std::abs (hfTop - 15.5), std::abs (lfBottom - 16.0), hfLow * 5.0, lfHigh * 5.0 });
        std::printf ("    shelves %+.0f: HF at 6 kHz reaches %.2f dB at 20 kHz (%.2f at 100 Hz); LF at 100 Hz %.2f dB at 20 Hz (%.2f at 10 kHz)\n",
                     g == 0 ? 18.0 : -18.0, hfTop, hfLow, lfBottom, lfHigh);
    }
    check (worst < 1.0, "full shelves: about +-15.5 dB (HF) and +-16 dB (LF) on the far side, nothing on the near side");
}

/** A 1 kHz sine at peakDb through the card: gain and harmonics (2nd-5th) over its second half. */
struct Tone { double gainDb, thdDb, outPeak; };
Tone tone (double fs, const Settings& s, double peakDb)
{
    auto m = makeSsl (fs, s);
    const int n = (int) fs;
    std::vector<float> x ((size_t) n);
    const double a = std::pow (10.0, peakDb / 20.0);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (a * std::sin (2.0 * kPi * 1000.0 * i / fs));
    runMono (*m, x);
    Tone t {};
    std::complex<double> h[6] {};
    for (int i = n / 2; i < n; ++i)
    {
        t.outPeak = std::max (t.outPeak, (double) std::abs (x[(size_t) i]));
        for (int k = 1; k <= 5; ++k) h[k] += (double) x[(size_t) i] * std::polar (1.0, -2.0 * kPi * 1000.0 * k * i / fs);
    }
    double harmonics = 0.0;
    for (int k = 2; k <= 5; ++k) harmonics += std::norm (h[k]);
    t.gainDb = db (2.0 * std::abs (h[1]) / (n / 2) / a);
    t.thdDb = 10.0 * std::log10 (harmonics / std::norm (h[1]) + 1e-30);
    return t;
}

void testLevel()
{
    std::printf ("Level (0 dBFS = +18 dBu, the 5534s linear to +-14.85 V)\n");
    for (double fs : kRates)
    {
        const Tone quiet = tone (fs, {}, -20.0);
        const Tone hot = tone (fs, {}, 4.0);
        const Tone clipped = tone (fs, {}, 12.0);
        check (std::abs (hot.gainDb - quiet.gainDb) < 0.01 && hot.thdDb < -100.0,
               fmt ("%.1f kHz, flat, 1 kHz: +4 dBFS in comes out like -20 dBFS (%.3f dB apart, < 0.01), harmonics %.0f dB (< -100)", fs / 1000.0, hot.gainDb - quiet.gainDb, hot.thdDb));
        check (clipped.outPeak < 1.9 && clipped.thdDb > -40.0,
               fmt ("%.1f kHz, flat, +12 dBFS in: clips at the rails, output peak %+.2f dBFS (16.5 V = +5.6 dBFS), harmonics %.0f dB", fs / 1000.0, db (clipped.outPeak), clipped.thdDb));
        const Settings filtersIn { { "hpf_on", 1.0f }, { "hpf_freq", 400.0f }, { "lpf_on", 1.0f }, { "lpf_freq", 3000.0f } };
        const Tone fQuiet = tone (fs, filtersIn, -20.0), fHot = tone (fs, filtersIn, 4.0);
        check (std::abs (fHot.gainDb - fQuiet.gainDb) < 0.01 && fHot.thdDb < -100.0,
               fmt ("%.1f kHz, HPF 400 Hz and LPF 3 kHz in: +4 dBFS linear like -20 dBFS (%.3f dB apart), harmonics %.0f dB", fs / 1000.0, fHot.gainDb - fQuiet.gainDb, fHot.thdDb));
    }
    const Tone boosted = tone (48000.0, { { "hmf_gain", 12.0f }, { "hmf_freq", 1000.0f } }, -8.0);
    std::printf ("    HMF +12 dB at 1 kHz, -8 dBFS in (48 kHz): %+.1f dB, harmonics %.0f dB (linear while the output stays under +4.6 dBFS)\n", boosted.gainDb, boosted.thdDb);
}

void testModulation()
{
    std::printf ("Controls changing while it runs\n");
    for (double fs : kRates)
    {
        auto m = makeSsl (fs, {});
        const auto specs = m->params();
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f), noise (-0.25f, 0.25f);
        std::vector<float> x ((size_t) (3.0 * fs));
        for (auto& v : x) v = noise (rng);
        for (size_t s = 0; s < x.size(); s += 32)
        {
            for (size_t i = 0; i < specs.size(); ++i)
            {
                const auto& sp = specs[i];
                const float u = unit (rng);
                m->setParam ((int) i, sp.type == ParamSpec::Type::choice ? std::round (u * sp.max)
                                                                        : (sp.logScale ? sp.min * std::pow (sp.max / sp.min, u) : sp.min + u * (sp.max - sp.min)));
            }
            m->process (x.data() + s, (int) std::min<size_t> (32, x.size() - s));
        }
        bool finite = true;
        double peak = 0.0;
        for (float v : x) { finite = finite && std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
        check (finite && peak < 4.0,
               fmt ("%.1f kHz: every control (switches too) to a random value every 32 samples, -12 dBFS noise: finite, peak %.2f (< 4)", fs / 1000.0, peak));
    }

    // Two instances (a stereo pair) given the same signal render the same, bit for bit.
    const Settings inUse { { "hpf_on", 1.0f }, { "lpf_on", 1.0f }, { "hf_gain", 6.0f }, { "hmf_gain", -9.0f }, { "lmf_gain", 4.0f }, { "lf_gain", 10.0f }, { "lf_bell", 1.0f } };
    auto a = makeSsl (48000.0, inUse), b = makeSsl (48000.0, inUse);
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> noise (-0.25f, 0.25f);
    std::vector<float> ya (48000), yb;
    for (auto& v : ya) v = noise (rng);
    yb = ya;
    runMono (*a, ya, 512);
    runMono (*b, yb, 100);
    check (ya == yb, "two instances, the same signal in blocks of 512 and of 100: bit for bit the same");
}

//== The filters ================================================================================
struct FilterRow
{
    double knob, fs;
    double corner = 0, expectedCorner = 0, slope = 0, slopeFs = 0, pass = 0, flatness = 0, peaking = 0;
};

/** The schematic's own values (tools/ssl_eq_ac.cpp filters / laws), for the expected columns:
    the -3 dB point where the knob is past the pot's end, and the passband (dB). */
constexpr double kHpfTopHz = 394.3, kHpfBottomHz = 15.146, kLpfBottomHz = 3186.5;
double circuitHpfPassband (double knob)
{
    // classic_channel_eq_ac filters: passband at 10 kHz, against the knob (RV5 trimmed for 0 dB at mid).
    return knob <= 30.0 ? 0.080 : knob <= 80.0 ? -0.011 : knob <= 200.0 ? -0.226 : -0.570;
}

void testFilters()
{
    std::printf ("Filters (EQ flat: each filter's response is the card's with the filter in over the card's with it out)\n");
    // High-pass at 48 and 96 kHz. Passband against 10 kHz; slope one to two octaves below the
    // corner; the impulse responses long enough (5.5 s) for 7.5 Hz.
    std::vector<FilterRow> hp;
    for (double fs : { 48000.0, 96000.0 })
        for (double k : { 30.0, 80.0, 200.0, 400.0 })
            hp.push_back ({ k, fs });
    parallelFor ((int) hp.size(), [&] (int i)
    {
        auto& r = hp[(size_t) i];
        const size_t length = r.fs > 60000.0 ? (1u << 19) : (1u << 18);
        const auto in = impulseOf (r.fs, { { "hpf_on", 1.0f }, { "hpf_freq", (float) r.knob } }, fx::SslSections::all, length);
        const auto out = impulseOf (r.fs, { { "hpf_freq", (float) r.knob } }, fx::SslSections::all, length);
        const auto g = [&] (double f) { return ratioDb (in, out, f, r.fs); };
        r.pass = g (10000.0);
        double lo = r.knob / 4.0, hi = r.knob * 4.0;
        for (int it = 0; it < 40; ++it) { const double m = std::sqrt (lo * hi); (g (m) < r.pass - 3.0 ? lo : hi) = m; }
        r.corner = std::sqrt (lo * hi);
        r.expectedCorner = std::clamp (r.knob, kHpfBottomHz, kHpfTopHz);
        r.slope = g (r.corner / 2.0) - g (r.corner / 4.0);
        r.slopeFs = r.fs;
        for (double f = 3.0 * r.corner; f <= 10000.0; f *= std::pow (2.0, 1.0 / 12.0))
        {
            r.flatness = std::max (r.flatness, std::abs (g (f) - r.pass));
            r.peaking = std::max (r.peaking, g (f) - r.pass);
        }
    });
    std::printf ("    HPF   knob Hz   fs kHz  -3 dB Hz  expected   error  slope dB/oct (18+-1.5)  passband dB  circuit  flat 3fc-10k\n");
    for (const auto& r : hp)
    {
        const double err = r.corner / r.expectedCorner - 1.0;
        const bool ok = std::abs (err) < 0.05 && std::abs (r.slope - 18.0) < 1.5 && std::abs (r.pass - circuitHpfPassband (r.knob)) < 0.05 && r.flatness < 0.2;
        std::printf ("  [%s] HPF %7.0f %8.1f %9.2f %9.2f %+6.2f%% %12.2f %24.3f %+8.3f %10.3f\n", ok ? "ok" : "FAIL", r.knob, r.fs / 1000.0, r.corner, r.expectedCorner,
                     100.0 * err, r.slope, r.pass, circuitHpfPassband (r.knob), r.flatness);
        ++checks;
        failures += ok ? 0 : 1;
    }
    std::printf ("    (expected: -3 dB within 5 %% of the knob, or of the pots' end where the knob is past it (394.3 Hz); passband within 0.05 dB of the\n"
                 "     schematic, which RV5 sets to 0 dB at mid position only; flat within 0.2 dB from 3x the corner to 10 kHz)\n");

    // Low-pass: corners at 48 and 96 kHz (the capacitors are pre-warped at the knob, so the
    // corner lands there at any rate). The slope one to two octaves above the corner is the
    // circuit's 12 dB/oct only where the trapezoidal rule's compression of the frequency axis is
    // small, so it is measured at a rate that puts 4 x corner under fs / 8; at 48 / 96 kHz the
    // rule makes it steeper (the response reaches -inf at Nyquist).
    std::vector<FilterRow> lp;
    for (double fs : { 48000.0, 96000.0 })
        for (double k : { 3000.0, 6000.0, 12000.0, 20000.0 })
            lp.push_back ({ k, fs });
    parallelFor ((int) lp.size(), [&] (int i)
    {
        auto& r = lp[(size_t) i];
        const auto in = impulseOf (r.fs, { { "lpf_on", 1.0f }, { "lpf_freq", (float) r.knob } }, fx::SslSections::all, 1u << 17);
        const auto out = impulseOf (r.fs, { { "lpf_freq", (float) r.knob } }, fx::SslSections::all, 1u << 17);
        const auto g = [&] (double f) { return ratioDb (in, out, f, r.fs); };
        r.pass = g (100.0);
        double lo = r.knob / 4.0, hi = std::min (r.knob * 4.0, 0.499 * r.fs);
        for (int it = 0; it < 40; ++it) { const double m = std::sqrt (lo * hi); (g (m) < r.pass - 3.0 ? hi : lo) = m; }
        r.corner = std::sqrt (lo * hi);
        r.expectedCorner = std::max (r.knob, kLpfBottomHz);
        for (double f = 20.0; f <= r.corner / 3.0; f *= std::pow (2.0, 1.0 / 12.0))
        {
            r.flatness = std::max (r.flatness, std::abs (g (f) - r.pass));
            r.peaking = std::max (r.peaking, g (f) - r.pass);
        }
        // The slope at 48 kHz x 2^n >= 32 x corner.
        double sfs = 48000.0;
        while (sfs < 32.0 * r.knob) sfs *= 2.0;
        const auto sin = impulseOf (sfs, { { "lpf_on", 1.0f }, { "lpf_freq", (float) r.knob } }, fx::SslSections::all, 1u << 16);
        const auto sout = impulseOf (sfs, { { "lpf_freq", (float) r.knob } }, fx::SslSections::all, 1u << 16);
        const auto sg = [&] (double f) { return ratioDb (sin, sout, f, sfs); };
        double slo = r.knob / 4.0, shi = r.knob * 4.0;
        const double spass = sg (100.0);
        for (int it = 0; it < 40; ++it) { const double m = std::sqrt (slo * shi); (sg (m) < spass - 3.0 ? shi : slo) = m; }
        const double sc = std::sqrt (slo * shi);
        r.slope = sg (2.0 * sc) - sg (4.0 * sc);
        r.slopeFs = sfs;
    });
    std::printf ("    LPF   knob Hz   fs kHz  -3 dB Hz  expected   error  slope dB/oct (12+-1.5) at fs  passband dB  circuit  flat 20-fc/3\n");
    for (const auto& r : lp)
    {
        const double err = r.corner / r.expectedCorner - 1.0;
        const bool ok = std::abs (err) < 0.05 && std::abs (r.slope - 12.0) < 1.5 && std::abs (r.pass + 0.008) < 0.05 && r.flatness < 0.2;
        std::printf ("  [%s] LPF %7.0f %8.1f %9.1f %9.1f %+6.2f%% %12.2f at %5.1f kHz %12.3f %+8.3f %10.3f\n", ok ? "ok" : "FAIL", r.knob, r.fs / 1000.0, r.corner, r.expectedCorner,
                     100.0 * err, r.slope, r.slopeFs / 1000.0, r.pass, -0.008, r.flatness);
        ++checks;
        failures += ok ? 0 : 1;
    }
    std::printf ("    (expected: -3 dB within 5 %% of the knob, or of the pots' end (3186.5 Hz) below it; the slope at the rate shown; passband at 100 Hz\n"
                 "     within 0.05 dB of the schematic's -0.008; flat within 0.2 dB below a third of the corner)\n");
}

/** Worst |dB| difference between two impulse responses, 20 Hz to min (20 kHz, 0.45 fs). */
double worstDifference (const std::vector<float>& a, const std::vector<float>& b, double fs)
{
    double worst = 0.0;
    for (double f = 20.0; f <= std::min (20000.0, 0.45 * fs); f *= std::pow (2.0, 1.0 / 12.0))
        worst = std::max (worst, std::abs (responseDb (a, f, fs) - responseDb (b, f, fs)));
    return worst;
}

void testSwitches()
{
    std::printf ("Switches\n");
    // Both filters out: the card is the EQ alone (the analog-recreation module's netlist, bit for bit
    // the analog-recreation ssl_e242), whatever the filter knobs say.
    const std::vector<Settings> eqSets { {}, { { "hf_gain", 18.0f }, { "hf_freq", 10000.0f }, { "hf_bell", 1.0f } }, { { "lf_gain", 16.0f }, { "lf_freq", 100.0f } },
                                         { { "hf_gain", 8.0f }, { "hmf_gain", -6.0f }, { "hmf_q", 3.0f }, { "lmf_gain", 5.0f }, { "lf_gain", 10.0f }, { "lf_bell", 1.0f } } };
    struct Pair { double fs; size_t set; double worst = 0; };
    std::vector<Pair> pairs;
    for (double fs : kRates)
        for (size_t s = 0; s < eqSets.size(); ++s)
            pairs.push_back ({ fs, s });
    parallelFor ((int) pairs.size(), [&] (int i)
    {
        auto& p = pairs[(size_t) i];
        auto withKnobs = eqSets[p.set];
        withKnobs.push_back ({ "hpf_freq", 400.0f });
        withKnobs.push_back ({ "lpf_freq", 3000.0f });
        p.worst = worstDifference (impulseOf (p.fs, withKnobs), impulseOf (p.fs, eqSets[p.set], fx::SslSections::eqOnly), p.fs);
    });
    for (double fs : kRates)
    {
        double worst = 0.0;
        for (const auto& p : pairs) if (p.fs == fs) worst = std::max (worst, p.worst);
        check (worst < 0.05, fmt ("%.1f kHz, both filters out (knobs at HPF 400 Hz, LPF 3 kHz): the EQ alone within %.5f dB, flat and three band settings (< 0.05)", fs / 1000.0, worst));
    }

    // Filters in, EQ out: the filters alone, whatever the band controls say.
    const std::vector<Settings> fSets { { { "hpf_freq", 80.0f }, { "lpf_freq", 12000.0f } }, { { "hpf_freq", 400.0f }, { "lpf_freq", 3000.0f } }, { { "hpf_freq", 15.0f }, { "lpf_freq", 22000.0f } } };
    std::vector<Pair> fPairs;
    for (double fs : kRates)
        for (size_t s = 0; s < fSets.size(); ++s)
            fPairs.push_back ({ fs, s });
    parallelFor ((int) fPairs.size(), [&] (int i)
    {
        auto& p = fPairs[(size_t) i];
        auto card = fSets[p.set];
        for (const auto& kv : Settings { { "hpf_on", 1.0f }, { "lpf_on", 1.0f }, { "eq_on", 0.0f }, { "hf_gain", 18.0f }, { "hf_bell", 1.0f }, { "lf_gain", -12.0f } })
            card.push_back (kv);
        auto alone = fSets[p.set];
        alone.push_back ({ "hpf_on", 1.0f });
        alone.push_back ({ "lpf_on", 1.0f });
        p.worst = worstDifference (impulseOf (p.fs, card), impulseOf (p.fs, alone, fx::SslSections::filtersOnly), p.fs);
    });
    for (double fs : kRates)
    {
        double worst = 0.0;
        for (const auto& p : fPairs) if (p.fs == fs) worst = std::max (worst, p.worst);
        check (worst < 0.05, fmt ("%.1f kHz, filters in, EQ out (HF +18, LF -12 set): the filters alone within %.5f dB, three filter settings (< 0.05)", fs / 1000.0, worst));
    }

    // Each switch flipped while a 1 kHz tone plays (48 kHz). The switches are the card's, hard
    // ones, and every section runs whether it is switched in or not, so a flip steps straight to
    // the new steady state: no burst, no settling beyond it.
    struct Flip { const char* what; Settings base; const char* id; };
    const std::vector<Flip> flips { { "EQ out -> in, flat", {}, "eq_on" },
                                    { "EQ out -> in, HMF +12 dB at 1 kHz", { { "hmf_gain", 12.0f }, { "hmf_freq", 1000.0f } }, "eq_on" },
                                    { "HPF out -> in at 400 Hz", { { "hpf_freq", 400.0f } }, "hpf_on" },
                                    { "LPF out -> in at 3 kHz", { { "lpf_freq", 3000.0f } }, "lpf_on" } };
    for (const auto& f : flips)
    {
        auto m = makeSsl (48000.0, f.base);
        int index = -1;
        const auto specs = m->params();
        for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == f.id) index = (int) i;
        m->setParam (index, 0.0f);
        m->reset();
        std::vector<float> x (48000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.25 * std::sin (2.0 * kPi * 1000.0 * (double) i / 48000.0));
        for (size_t s = 0; s < x.size(); s += 64)
        {
            if (s == 24000) m->setParam (index, 1.0f);
            m->process (x.data() + s, 64);
        }
        const auto peak = [&x] (size_t a, size_t b) { double p = 0.0; for (size_t i = a; i < b; ++i) p = std::max (p, (double) std::abs (x[i])); return p; };
        const double before = peak (12000, 24000), atSwitch = peak (24000, 26400), settled = peak (36000, 48000);
        char text[512];
        std::snprintf (text, sizeof (text), "%s: peak %.3f before, %.3f in the 50 ms after the flip, %.3f settled (no more than the larger + 2 %%)",
                       f.what, before, atSwitch, settled);
        check (atSwitch < 1.02 * std::max (before, settled), text);
    }
}

void testSilence()
{
    std::printf ("Silence after a loud burst\n");
    for (double fs : { 48000.0, 96000.0 })
    {
        auto m = makeSsl (fs, { { "hpf_on", 1.0f }, { "hpf_freq", 30.0f }, { "lpf_on", 1.0f }, { "hf_gain", 18.0f }, { "hf_bell", 1.0f }, { "hmf_gain", 18.0f },
                                { "lmf_gain", 18.0f }, { "lf_gain", 18.0f }, { "lf_bell", 1.0f } });
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> loud (-2.0f, 2.0f);
        std::vector<float> burst ((size_t) (0.5 * fs));
        for (auto& v : burst) v = loud (rng);
        runMono (*m, burst);
        bool finite = true;
        double firstSecond = 0.0, lastSecond = 0.0, lastPeak = 0.0;
        std::vector<float> silence ((size_t) fs);
        for (int sec = 0; sec < 10; ++sec)
        {
            std::fill (silence.begin(), silence.end(), 0.0f);
            const auto t0 = std::chrono::steady_clock::now();
            runMono (*m, silence);
            const double t = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            (sec == 0 ? firstSecond : lastSecond) = t;
            double peak = 0.0;
            for (float v : silence) { finite = finite && std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
            lastPeak = peak;
        }
        check (finite && lastPeak < 1e-4 && lastSecond < 3.0 * firstSecond,
               fmt ("%.0f kHz, every band +18, filters in, 0.5 s of +6 dBFS noise then 10 s of silence: finite, last second's peak %.2g (< 1e-4), "
                    "last second took %.2fx the first's CPU (< 3: no denormal slow-down)",
                    fs / 1000.0, lastPeak, lastSecond / firstSecond));
    }
}

void testDynamics()
{
    std::printf ("82E10 T82010.71 issue-15 dynamics\n");
    constexpr double fs = 48000.0;
    const auto run = [sampleRate = fs] (fx::SslE10Settings settings, float amplitude, double seconds, float rightScale = 1.0f)
    {
        fx::SslE10Dynamics dynamics;
        dynamics.prepare (sampleRate);
        dynamics.setSettings (settings);
        const int n = (int) (seconds * sampleRate);
        std::vector<float> left ((size_t) n), right ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) i] = amplitude * std::sin ((float) (2.0 * kPi * 1000.0 * i / sampleRate));
            right[(size_t) i] = rightScale * left[(size_t) i];
        }
        for (int start = 0; start < n; start += 257)
            dynamics.process (left.data() + start, right.data() + start, left.data() + start, right.data() + start, std::min (257, n - start));
        double in = 0.0, outL = 0.0, outR = 0.0;
        for (int i = n / 2; i < n; ++i)
        {
            const double x = amplitude * std::sin (2.0 * kPi * 1000.0 * i / sampleRate);
            in += x * x;
            outL += left[(size_t) i] * left[(size_t) i];
            outR += right[(size_t) i] * right[(size_t) i];
        }
        return std::array<double, 5> { db (std::sqrt (outL / in)), db (std::sqrt (outR / (in * rightScale * rightScale))),
                                       dynamics.compressorGainReductionDb(), dynamics.gateGainReductionDb(), rightScale };
    };

    fx::SslE10Settings bypass;
    bypass.enabled = false;
    const auto dry = run (bypass, 0.25f, 0.2);
    check (std::abs (dry[0]) < 1e-5, fmt ("bypass is exact: %+.6f dB", dry[0]));

    fx::SslE10Settings comp;
    comp.enabled = true;
    comp.automaticMakeup = false;
    comp.compressorThresholdDb = -10.0f;
    comp.compressorRatio = 4.0f;
    comp.compressorReleaseSeconds = 0.1f;
    const auto compressed = run (comp, 0.25f, 1.0, 0.1f);
    check (compressed[0] < -8.0 && std::abs (compressed[0] - compressed[1]) < 0.02 && compressed[2] > 8.0,
           fmt ("4:1 above threshold: %.2f dB level change, stereo-linked right %.2f dB, %.2f dB GR", compressed[0], compressed[1], compressed[2]));

    comp.compressorRatio = 1.0f;
    const auto unityRatio = run (comp, 0.25f, 0.5);
    check (std::abs (unityRatio[0]) < 0.02, fmt ("1:1 ratio is unity: %+.3f dB", unityRatio[0]));

    fx::SslE10Settings gate;
    gate.enabled = true;
    gate.compressorRatio = 1.0f;
    gate.gateThresholdDb = 10.0f;
    gate.gateRangeDb = 40.0f;
    gate.gateReleaseSeconds = 0.1f;
    const auto gated = run (gate, 0.01f, 1.0);
    gate.expander = true;
    const auto expanded = run (gate, 0.01f, 1.0);
    check (gated[0] < -35.0 && gated[3] > 35.0 && expanded[0] > gated[0] + 15.0,
           fmt ("gate reaches %.2f dB (meter %.2f); 2:1 expander is gentler at %.2f dB", gated[0], gated[3], expanded[0]));

    const auto attackSeconds = [sampleRate = fs] (bool fast)
    {
        fx::SslE10Dynamics dynamics;
        dynamics.prepare (sampleRate);
        fx::SslE10Settings s;
        s.enabled = true;
        s.automaticMakeup = false;
        s.compressorThresholdDb = -20.0f;
        s.compressorRatio = 20.0f;
        s.compressorFastAttack = fast;
        dynamics.setSettings (s);
        for (int i = 0; i < (int) (0.5 * sampleRate); ++i)
        {
            float x = 0.5f * std::sin ((float) (2.0 * kPi * 1000.0 * i / sampleRate));
            dynamics.process (&x, nullptr, &x, nullptr, 1);
            if (dynamics.compressorGainReductionDb() >= 10.0f)
                return i / sampleRate;
        }
        return 0.5;
    };
    const double autoAttack = attackSeconds (false), fastAttack = attackSeconds (true);
    check (fastAttack < autoAttack * 0.5 && fastAttack < 0.010 && autoAttack > 0.010,
           fmt ("R21/C5 AUTO reaches 10 dB GR in %.2f ms; R19/C5 FAST in %.2f ms",
                autoAttack * 1000.0, fastAttack * 1000.0));
}

//== The clippers ===============================================================================
void testClippers()
{
    std::printf ("Clippers (the op-amps' linear limit)\n");
    // The clippers: an exact pass-through below the op-amps' linear limit, an overload report above it.
    for (double fs : kRates)
    {
        auto m = makeSsl (fs, {});
        const auto driven = [&] (double dbfs)
        {
            std::vector<float> x ((size_t) fs);
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (std::pow (10.0, dbfs / 20.0) * std::sin (2.0 * kPi * 1000.0 * (double) i / fs));
            m->takeOverloadVolts();
            runMono (*m, x);
            return m->takeOverloadVolts();
        };
        const float quiet = driven (4.0), loud = driven (8.0);
        check (quiet == 0.0f && loud > 0.9f * 14.85f && loud < 1.01f * std::pow (10.0, 8.0 / 20.0) * fx::kSslVoltsPerFullScale,
               fmt ("%.1f kHz: +4 dBFS (flat) never reaches the clippers (report %.2f V); +8 dBFS reports %.2f V (peak 21.9 V at the card, over 14.85 V)", fs / 1000.0, (double) quiet, (double) loud));
    }
}

void testCpu()
{
    std::printf ("CPU (x realtime, mono, one core, -12 dBFS noise, 5 s)\n");
    const auto time = [] (double fs, const Settings& s)
    {
        auto m = makeSsl (fs, s);
        std::vector<float> x ((size_t) (5.0 * fs));
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> noise (-0.25f, 0.25f);
        for (auto& v : x) v = noise (rng);
        const auto t0 = std::chrono::steady_clock::now();
        runMono (*m, x);
        return 5.0 / std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const Settings busy { { "hpf_on", 1.0f }, { "lpf_on", 1.0f }, { "hf_gain", 8.0f }, { "hf_bell", 1.0f }, { "hmf_gain", -6.0f }, { "lmf_gain", 5.0f }, { "lf_gain", 10.0f } };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        std::printf ("    %.1f kHz: %.1fx at the defaults (filters out), %.1fx with both filters in and every band in use\n", fs / 1000.0, time (fs, {}), time (fs, busy));
}
} // namespace

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::strcmp (argv[1], "--quick") == 0;
    const auto t0 = std::chrono::steady_clock::now();
    testControls();
    if (! quick)
    {
        testFlat();
        testBands();
        testLevel();
        testModulation();
    }
    testFilters();
    testSwitches();
    if (! quick)
        testClippers();
    testDynamics();
    testSilence();
    if (! quick)
        testCpu();
    std::printf ("%d checks, %s (%.0f s)\n", checks, failures == 0 ? "all passed" : (std::to_string (failures) + " FAILED").c_str(),
                 std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    return failures == 0 ? 0 : 1;
}
