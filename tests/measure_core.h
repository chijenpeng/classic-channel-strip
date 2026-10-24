// Spot checks of the SSL 4000 E card model, no JUCE.
//
//   classic_channel_measure fs [id=value ...]            magnitude response (dB) at 1/12-octave points from
//                                            20 Hz to 20 kHz, from an impulse response
//   classic_channel_measure fs [id=value ...] --sine hz dbfs
//                                            a sine at hz and dbfs (peak) through the card: output
//                                            peak (dBFS), gain and THD (2nd-9th harmonics)
//   classic_channel_measure fs [id=value ...] --time     how fast 10 s of -12 dBFS noise renders (x realtime)
//   classic_channel_measure --list                       the controls, their ranges and defaults
//
// Controls by id (sessions' ids, e.g. hpf_on=1 hpf_freq=80 hf_gain=18 hf_bell=1); choices by
// index (Out / In: 0 / 1; Shelf / Bell: 0 / 1). Anything not given is at its default.

#pragma once

#include "dsp/SslE242.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace ar;

namespace measure
{
constexpr double kPi = 3.14159265358979323846;

inline void runBlocks (Module& m, std::vector<float>& x)
{
    for (size_t s = 0; s < x.size(); s += 512)
        m.process (x.data() + s, (int) std::min<size_t> (512, x.size() - s));
}

inline double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-30)); }

inline std::complex<double> response (const std::vector<float>& h, double hz, double fs)
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

/** How a variant of the tool differs: which module it measures and how many timing passes it can afford. */
struct Hooks
{
    std::function<ModulePtr()> make;   // empty: the plug-in's card
    int timingPasses = 7;
};

inline int run (int argc, char** argv, const Hooks& hooks)
{
    if (argc > 1 && std::strcmp (argv[1], "--list") == 0)
    {
        for (const auto& s : fx::sslE242Params())
        {
            std::printf ("%-9s %-10s ", s.id.c_str(), s.name.c_str());
            if (s.type == ParamSpec::Type::choice)
            {
                for (size_t i = 0; i < s.choices.size(); ++i) std::printf ("%zu=%s ", i, s.choices[i].c_str());
                std::printf ("(default %g)\n", s.def);
            }
            else
                std::printf ("%g..%g %s%s (default %g)\n", s.min, s.max, s.unit.c_str(), s.logScale ? ", log" : "", s.def);
        }
        return 0;
    }
    if (argc < 2)
    {
        std::printf ("usage: classic_channel_measure fs [id=value ...] [--sine hz dbfs | --time] | classic_channel_measure --list\n");
        return 1;
    }
    const double fs = std::atof (argv[1]);
    auto m = hooks.make ? hooks.make() : fx::makeSslE242 (fx::SslSections::all);
    m->prepare (fs);
    const auto specs = m->params();
    std::vector<float> values;
    for (const auto& s : specs) values.push_back (s.def);
    double sineHz = 0.0, sineDb = 0.0;
    bool timing = false;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--sine" && i + 2 < argc) { sineHz = std::atof (argv[i + 1]); sineDb = std::atof (argv[i + 2]); i += 2; continue; }
        if (a == "--time") { timing = true; continue; }
        const auto eq = a.find ('=');
        bool found = false;
        for (size_t k = 0; eq != std::string::npos && k < specs.size(); ++k)
            if (specs[k].id == a.substr (0, eq)) { values[k] = (float) std::atof (a.c_str() + eq + 1); found = true; }
        if (! found) { std::printf ("unknown control or option: %s (classic_channel_measure --list)\n", a.c_str()); return 1; }
    }
    for (size_t k = 0; k < specs.size(); ++k)
        m->setParam ((int) k, values[k]);
    m->reset();

    if (timing)
    {
        std::vector<float> x ((size_t) (10.0 * fs));
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> noise (-0.25f, 0.25f);
        for (auto& v : x) v = noise (rng);
        // Best of several passes (the state-space engine needs well under a second for 10 s).
        const int passes = hooks.timingPasses;
        double seconds = 1e30;
        for (int pass = 0; pass < passes; ++pass)
        {
            auto copy = x;
            const auto t0 = std::chrono::steady_clock::now();
            runBlocks (*m, copy);
            seconds = std::min (seconds, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        std::printf ("10 s at %.0f Hz, mono, -12 dBFS noise: %.3f s, %.1fx realtime%s\n", fs, seconds, 10.0 / seconds, passes > 1 ? " (best of 7)" : "");
        return 0;
    }
    if (sineHz > 0.0)
    {
        // Two seconds; the second analysed (whole cycles of a 1 Hz grid, so any hz works).
        const int n = (int) (2.0 * fs), from = n / 2;
        const double a = std::pow (10.0, sineDb / 20.0);
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (a * std::sin (2.0 * kPi * sineHz * i / fs));
        runBlocks (*m, x);
        double peak = 0.0, harmonics = 0.0;
        std::complex<double> h1;
        for (int k = 1; k <= 9 && k * sineHz < 0.5 * fs; ++k)
        {
            std::complex<double> acc;
            for (int i = from; i < n; ++i) acc += (double) x[(size_t) i] * std::polar (1.0, -2.0 * kPi * sineHz * k * i / fs);
            if (k == 1) h1 = acc; else harmonics += std::norm (acc);
        }
        for (int i = from; i < n; ++i) peak = std::max (peak, (double) std::abs (x[(size_t) i]));
        std::printf ("%.1f Hz at %+.1f dBFS peak (%.2f V at the card): output peak %+.2f dBFS, gain %+.3f dB, THD (2nd-9th) %.1f dB (%.4f %%)\n",
                     sineHz, sineDb, a * fx::kSslVoltsPerFullScale, db (peak), db (2.0 * std::abs (h1) / (n - from) / a),
                     10.0 * std::log10 (harmonics / std::norm (h1) + 1e-30), 100.0 * std::sqrt (harmonics / std::norm (h1)));
        return 0;
    }

    std::vector<float> h ((size_t) (fs > 60000.0 ? (1u << 18) : (1u << 17)), 0.0f);
    h[0] = 0.01f;
    runBlocks (*m, h);
    for (auto& v : h) v /= 0.01f;
    std::printf ("#  Hz        dB   (fs %.0f)\n", fs);
    for (int i = 0; i <= 120; ++i)
    {
        const double f = 20.0 * std::pow (2.0, i / 12.0);
        if (f > 20001.0 || f >= 0.5 * fs) break;
        std::printf ("%8.1f %+8.3f\n", f, db (std::abs (response (h, f, fs))));
    }
    return 0;
}
} // namespace measure
