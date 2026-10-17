// The plug-in's processor offline (JUCE, no host, no editor): parameters, state, mono and stereo,
// latency and the oversampling switch, the response through processBlock at 1x and 2x, and
// allocations on the audio thread. Prints what it measures and exits non-zero on a failure.
//
//   classic_channel_processor_test

#include "../Source/PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

//== Allocation counting: every operator new on a thread that has counting switched on ==========
namespace
{
std::atomic<long> allocations { 0 };
thread_local bool counting = false;
}

void* operator new (std::size_t size)
{
    if (counting)
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double kPi = 3.14159265358979323846;
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what.toRawUTF8());
    if (! ok)
        ++failures;
}

void set (Ssl4000EqProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** Runs `samples` through the processor in blocks; counts allocations inside processBlock. */
long run (Ssl4000EqProcessor& p, juce::AudioBuffer<float>& io, int block)
{
    juce::MidiBuffer midi;
    const long before = allocations.load();
    for (int s = 0; s < io.getNumSamples(); s += block)
    {
        const int n = juce::jmin (block, io.getNumSamples() - s);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), s, n);
        counting = true;
        p.processBlock (view, midi);
        counting = false;
    }
    return allocations.load() - before;
}

std::complex<double> response (const float* h, int n, double hz, double fs)
{
    std::complex<double> sum;
    for (int i = 0; i < n; ++i)
        sum += (double) h[i] * std::polar (1.0, -2.0 * kPi * hz * i / fs);
    return sum;
}

using Settings = std::vector<std::pair<const char*, float>>;

/** A fresh processor, stereo at 48 kHz, oversampling as given, with `s`. */
std::unique_ptr<Ssl4000EqProcessor> make (bool twoX, const Settings& s, double fs = 48000.0, int block = 512)
{
    auto p = std::make_unique<Ssl4000EqProcessor>();
    set (*p, ParamID::oversample, twoX ? 1.0f : 0.0f);
    for (const auto& kv : s)
        set (*p, kv.first, kv.second);
    p->setPlayConfigDetails (2, 2, fs, block);
    p->prepareToPlay (fs, block);
    return p;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    std::printf ("Parameters and state\n");
    {
        Ssl4000EqProcessor p;
        const char* order[] = { "input", "hpf_on", "hpf_freq", "lpf_on", "lpf_freq", "hf_gain", "hf_freq", "hf_bell", "hmf_gain", "hmf_freq", "hmf_q",
                                "lmf_gain", "lmf_freq", "lmf_q", "lf_gain", "lf_freq", "lf_bell", "eq_on", "output", "oversample",
                                "dyn_on", "comp_threshold", "comp_ratio", "comp_release", "comp_fast", "comp_makeup", "gate_threshold", "gate_range",
                                "gate_release", "gate_mode", "gate_fast", "dyn_position", "sidechain_source" };
        const auto& params = p.getParameters();
        bool ok = params.size() == 33;
        for (int i = 0; ok && i < 33; ++i)
            ok = dynamic_cast<juce::RangedAudioParameter*> (params[i])->getParameterID() == order[i];
        check (ok, "33 stable parameters: the original 20 first, then the complete 82E10 dynamics and routing controls");
        const auto* hpf = p.apvts.getParameter ("hpf_freq");
        const auto* lpf = p.apvts.getParameter ("lpf_freq");
        check (std::abs (hpf->convertFrom0to1 (0.5f) - std::sqrt (16.0f * 390.0f)) < 0.1f && std::abs (lpf->convertFrom0to1 (0.5f) - std::sqrt (3200.0f * 22000.0f)) < 1.0f
                   && hpf->getText (hpf->getDefaultValue(), 20) == "80.0 Hz" && lpf->getText (lpf->getDefaultValue(), 20) == "12.0 kHz",
               "frequency knobs sweep geometrically (middle of HPF " + juce::String (hpf->convertFrom0to1 (0.5f), 2) + " Hz, LPF " + juce::String (lpf->convertFrom0to1 (0.5f), 0)
                   + " Hz: the geometric means); defaults read " + hpf->getText (hpf->getDefaultValue(), 20) + " and " + lpf->getText (lpf->getDefaultValue(), 20));
        set (p, "hf_gain", 7.5f);
        set (p, "lpf_on", 1.0f);
        set (p, "hpf_freq", 120.0f);
        set (p, "oversample", 0.0f);
        set (p, ParamID::dynOn, 1.0f);
        set (p, ParamID::compRatio, 4.0f);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        Ssl4000EqProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        const auto v = [&q] (const char* id) { return q.apvts.getRawParameterValue (id)->load(); };
        check (std::abs (v ("hf_gain") - 7.5f) < 1e-3f && v ("lpf_on") == 1.0f && std::abs (v ("hpf_freq") - 120.0f) < 0.01f && v ("oversample") == 0.0f
                   && v (ParamID::dynOn) == 1.0f && std::abs (v (ParamID::compRatio) - 4.0f) < 1e-3f,
               "EQ and dynamics state saved and restored through the APVTS");

        juce::MemoryBlock oldState;
        p.getStateInformation (oldState);
        auto oldXml = juce::AudioProcessor::getXmlFromBinary (oldState.getData(), (int) oldState.getSize());
        for (const char* id : { ParamID::dynOn, ParamID::compThreshold, ParamID::compRatio, ParamID::compRelease, ParamID::compFast,
                                ParamID::compMakeup, ParamID::gateThreshold, ParamID::gateRange, ParamID::gateRelease, ParamID::gateMode,
                                ParamID::gateFast, ParamID::dynPosition, ParamID::sidechainSource })
            if (auto* child = oldXml->getChildByAttribute ("id", id))
                oldXml->removeChildElement (child, true);
        juce::AudioProcessor::copyXmlToBinary (*oldXml, oldState);
        set (q, ParamID::dynOn, 1.0f); set (q, ParamID::gateRange, 40.0f);
        q.setStateInformation (oldState.getData(), (int) oldState.getSize());
        check (v (ParamID::dynOn) == 0.0f && std::abs (v (ParamID::compRatio) - 2.0f) < 1e-3f && v (ParamID::gateRange) == 0.0f,
               "an EQ-only session with no dynamics properties migrates to safe defaults");
    }

    std::printf ("Mono and stereo, latency\n");
    {
        Ssl4000EqProcessor p;
        juce::AudioProcessor::BusesLayout mono, stereo, stereoSc, mixed;
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.inputBuses.add (juce::AudioChannelSet::disabled()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        stereo.inputBuses.add (juce::AudioChannelSet::stereo()); stereo.inputBuses.add (juce::AudioChannelSet::disabled()); stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        stereoSc.inputBuses.add (juce::AudioChannelSet::stereo()); stereoSc.inputBuses.add (juce::AudioChannelSet::mono()); stereoSc.outputBuses.add (juce::AudioChannelSet::stereo());
        mixed.inputBuses.add (juce::AudioChannelSet::mono()); mixed.inputBuses.add (juce::AudioChannelSet::disabled()); mixed.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (mono) && p.checkBusesLayoutSupported (stereo) && p.checkBusesLayoutSupported (stereoSc)
                   && ! p.checkBusesLayoutSupported (mixed),
               "mono/stereo main buses, plus disabled, mono or stereo external sidechain");

        auto one = make (false, {});
        auto two = make (true, {});
        check (one->getLatencySamples() == 0 && two->getLatencySamples() > 0 && two->getActiveOversampling() == 2,
               "latency " + juce::String (one->getLatencySamples()) + " samples at 1x, " + juce::String (two->getLatencySamples()) + " at 2x (integer, reported to the host)");

        // Stereo: two identical cards.
        auto s = make (true, { { "hpf_on", 1.0f }, { "hf_gain", 9.0f }, { "lf_gain", -6.0f }, { "lf_bell", 1.0f } });
        juce::AudioBuffer<float> io (2, 48000);
        juce::Random rng (3);
        for (int i = 0; i < io.getNumSamples(); ++i)
        {
            const float x = rng.nextFloat() * 0.5f - 0.25f;
            io.setSample (0, i, x);
            io.setSample (1, i, x);
        }
        run (*s, io, 512);
        bool same = true;
        for (int i = 0; i < io.getNumSamples(); ++i)
            same = same && io.getSample (0, i) == io.getSample (1, i);
        check (same, "stereo: the same signal on both channels comes out the same on both, bit for bit");

        // Mono layout.
        Ssl4000EqProcessor m;
        m.setBusesLayout (mono);
        m.setPlayConfigDetails (1, 1, 48000.0, 256);
        m.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> mio (1, 4800);
        for (int i = 0; i < mio.getNumSamples(); ++i) mio.setSample (0, i, (float) (0.5 * std::sin (2.0 * kPi * 1000.0 * i / 48000.0)));
        run (m, mio, 256);
        const float peak = mio.getMagnitude (0, 2400, 2400);
        check (std::abs (juce::Decibels::gainToDecibels (peak / 0.5f)) < 0.2f, "mono: a 1 kHz tone through the flat card at 2x keeps its level (" + juce::String (juce::Decibels::gainToDecibels (peak / 0.5f), 3) + " dB)");
    }

    std::printf ("Meters\n");
    {
        // Input -20 dB, Output -6 dB: the input meter reads before the trim, the output after.
        auto p = make (true, { { ParamID::input, -20.0f }, { ParamID::output, -6.0f } });
        juce::AudioBuffer<float> io (2, 9600);
        for (int i = 0; i < io.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                io.setSample (ch, i, (float) ((ch == 0 ? 0.5 : 0.25) * std::sin (2.0 * kPi * 1000.0 * i / 48000.0)));
        const long allocs = run (*p, io, 512);
        const float inL = p->takeInputPeak (0), inR = p->takeInputPeak (1), outL = p->takeOutputPeak (0);
        const float outDb = juce::Decibels::gainToDecibels (outL / 0.5f);
        check (std::abs (inL - 0.5f) < 1e-3f && std::abs (inR - 0.25f) < 1e-3f && std::abs (outDb + 26.0f) < 0.3f
                   && p->takeInputPeak (0) == 0.0f && p->takeOutputPeak (0) == 0.0f && allocs == 0,
               "input peaks " + juce::String (inL, 3) + " / " + juce::String (inR, 3) + " (before the -20 dB trim), output "
                   + juce::String (outDb, 2) + " dB re the input (trims -26 dB); both start over once taken");
    }

    std::printf ("82E10 dynamics and routing\n");
    {
        const auto sineLevel = [] (Ssl4000EqProcessor& p, float amplitude, int channel = 0, double hz = 1000.0)
        {
            juce::AudioBuffer<float> io (2, 48000);
            for (int i = 0; i < io.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    io.setSample (ch, i, amplitude * (ch == channel || channel < 0 ? 1.0f : 0.1f)
                                              * (float) std::sin (2.0 * kPi * hz * i / 48000.0));
            run (p, io, 512);
            double sum = 0.0;
            for (int i = 24000; i < 48000; ++i) sum += io.getSample (0, i) * io.getSample (0, i);
            return juce::Decibels::gainToDecibels ((float) std::sqrt (sum / 24000.0));
        };

        auto dry = make (false, {});
        auto comp = make (false, { { ParamID::dynOn, 1.0f }, { ParamID::compThreshold, -10.0f }, { ParamID::compRatio, 4.0f },
                                   { ParamID::compMakeup, 0.0f }, { ParamID::compRelease, 0.1f } });
        const float dryDb = sineLevel (*dry, 0.25f), compDb = sineLevel (*comp, 0.25f, -1);
        const float compGr = comp->takeCompressorReduction();
        check (compDb < dryDb - 8.0f && compGr > 8.0f,
               "4:1 compressor reduces a -12 dBFS peak tone from " + juce::String (dryDb, 2) + " to " + juce::String (compDb, 2)
                   + " dBFS; meter " + juce::String (compGr, 2) + " dB");

        auto gateOff = make (false, { { ParamID::dynOn, 1.0f }, { ParamID::compRatio, 1.0f } });
        auto legacyGate = make (false, { { ParamID::dynOn, 1.0f }, { ParamID::compRatio, 1.0f },
                                         { ParamID::gateThreshold, 10.0f }, { ParamID::gateRange, 40.0f },
                                         { ParamID::gateRelease, 0.1f }, { ParamID::gateMode, 1.0f } });
        const float gateOffDb = sineLevel (*gateOff, 0.01f);
        const float legacyGateDb = sineLevel (*legacyGate, 0.01f);
        check (std::abs (legacyGateDb - gateOffDb) < 0.1f,
               "legacy gate settings cannot attenuate audio (difference " + juce::String (legacyGateDb - gateOffDb, 3) + " dB)");

        const Settings routeBase { { ParamID::dynOn, 1.0f }, { ParamID::compThreshold, -10.0f }, { ParamID::compRatio, 4.0f },
                                   { ParamID::compMakeup, 0.0f }, { "hmf_gain", 18.0f }, { "hmf_freq", 1000.0f } };
        auto pre = make (false, routeBase);
        auto post = make (false, Settings (routeBase));
        set (*post, ParamID::dynPosition, 1.0f);
        sineLevel (*pre, 0.10f, -1); sineLevel (*post, 0.10f, -1);
        const float preGr = pre->takeCompressorReduction(), postGr = post->takeCompressorReduction();
        check (postGr > preGr + 5.0f,
               "post-EQ routing lets the boosted EQ drive the detector (" + juce::String (preGr, 2) + " dB pre, " + juce::String (postGr, 2) + " dB post)");

        auto inputKey = make (false, { { ParamID::dynOn, 1.0f }, { ParamID::compThreshold, -20.0f }, { ParamID::compRatio, 10.0f },
                                      { ParamID::compMakeup, 0.0f }, { "hpf_on", 1.0f }, { "hpf_freq", 390.0f } });
        auto filterKey = make (false, { { ParamID::dynOn, 1.0f }, { ParamID::compThreshold, -20.0f }, { ParamID::compRatio, 10.0f },
                                       { ParamID::compMakeup, 0.0f }, { "hpf_on", 1.0f }, { "hpf_freq", 390.0f }, { ParamID::sidechainSource, 1.0f } });
        sineLevel (*inputKey, 0.25f, -1, 50.0); sineLevel (*filterKey, 0.25f, -1, 50.0);
        const float inputGr = inputKey->takeCompressorReduction(), filterGr = filterKey->takeCompressorReduction();
        check (filterGr < inputGr - 8.0f,
               "FILTERS sidechain removes low-frequency key energy (" + juce::String (inputGr, 2) + " dB input key, " + juce::String (filterGr, 2) + " dB filtered key)");

        // A mono external key drives a stereo main bus, while the quiet main input alone stays
        // below the compressor knee. This also exercises JUCE's optional input bus mapping.
        Ssl4000EqProcessor external;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        check (external.setBusesLayout (layout), "external sidechain layout accepted by the processor");
        set (external, ParamID::dynOn, 1.0f); set (external, ParamID::compThreshold, -20.0f); set (external, ParamID::compRatio, 20.0f);
        set (external, ParamID::compMakeup, 0.0f); set (external, ParamID::sidechainSource, 3.0f); set (external, ParamID::oversample, 0.0f);
        external.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> keyed (3, 48000);
        for (int i = 0; i < keyed.getNumSamples(); ++i)
        {
            const float phase = (float) std::sin (2.0 * kPi * 1000.0 * i / 48000.0);
            keyed.setSample (0, i, 0.01f * phase); keyed.setSample (1, i, 0.01f * phase); keyed.setSample (2, i, 0.8f * phase);
        }
        const long extAllocs = run (external, keyed, 512);
        double extSum = 0.0;
        for (int i = 24000; i < 48000; ++i) extSum += keyed.getSample (0, i) * keyed.getSample (0, i);
        const float extDb = juce::Decibels::gainToDecibels ((float) std::sqrt (extSum / 24000.0));
        check (extDb < -60.0f && external.takeCompressorReduction() > 15.0f && extAllocs == 0,
               "external mono key compresses linked stereo without audio-thread allocation (output " + juce::String (extDb, 2) + " dBFS)");
    }

    std::printf ("Responses through processBlock at 48 kHz (dB; impulse at -40 dBFS, 2.7 s)\n");
    {
        struct Case { const char* name; Settings s; };
        const std::vector<Case> cases { { "flat", {} },
                                        { "HPF 80 Hz in", { { "hpf_on", 1.0f }, { "hpf_freq", 80.0f } } },
                                        { "LPF 12 kHz in", { { "lpf_on", 1.0f }, { "lpf_freq", 12000.0f } } },
                                        { "HF +18 bell 10k", { { "hf_gain", 18.0f }, { "hf_freq", 10000.0f }, { "hf_bell", 1.0f } } } };
        const double freqs[] = { 20, 40, 80, 160, 1000, 5000, 8000, 10000, 12000, 14000, 16000, 18000, 20000 };
        std::printf ("  %-18s %3s", "", "");
        for (double f : freqs) std::printf (" %7.0f", f);
        std::printf ("\n");
        for (const auto& c : cases)
            for (bool twoX : { false, true })
            {
                auto p = make (twoX, c.s);
                juce::AudioBuffer<float> io (2, 1 << 17);
                io.clear();
                io.setSample (0, 0, 0.01f);
                io.setSample (1, 0, 0.01f);
                run (*p, io, 512);
                const float* h = io.getReadPointer (0);
                std::printf ("  %-18s %3s", c.name, twoX ? "2x" : "1x");
                for (double f : freqs)
                    std::printf (" %+7.2f", 20.0 * std::log10 (std::abs (response (h, io.getNumSamples(), f, 48000.0)) / 0.01));
                std::printf ("\n");
            }
    }

    std::printf ("The audio thread\n");
    {
        auto p = make (true, { { "hpf_on", 1.0f }, { "lpf_on", 1.0f } });
        juce::AudioBuffer<float> io (2, 48000);
        const auto fill = [&io] { juce::Random r (9); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < io.getNumSamples(); ++i) io.setSample (ch, i, r.nextFloat() * 0.5f - 0.25f); };
        fill();
        run (*p, io, 512);   // the first block sends every parameter
        fill();
        const long steady = run (*p, io, 512);
        // Every control moving: each block, a new value for every parameter.
        juce::MidiBuffer midi;
        long moving = 0;
        juce::Random r (4);
        for (int s = 0; s < 48000; s += 512)
        {
            for (auto* param : p->getParameters())
                if (dynamic_cast<juce::RangedAudioParameter*> (param)->getParameterID() != ParamID::oversample)
                    param->setValueNotifyingHost (r.nextFloat());
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, s, juce::jmin (512, 48000 - s));
            fill();
            const long before = allocations.load();
            counting = true;
            p->processBlock (view, midi);
            counting = false;
            moving += allocations.load() - before;
        }
        check (steady == 0, "no allocations in processBlock with the controls still (" + juce::String (steady) + " in 94 blocks)");
        std::printf ("    with every control moving every block: %ld allocations in 94 blocks (the circuit engine's refactor: see the README)\n", moving);

        // Back to known settings, and let the cards' smoothing finish.
        for (auto* param : p->getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
        set (*p, "hpf_on", 1.0f);
        set (*p, "lpf_on", 1.0f);
        fill();
        run (*p, io, 512);

        // A host block larger than prepareToPlay announced.
        juce::AudioBuffer<float> big (2, 3000);
        big.clear();
        const long bigAllocs = run (*p, big, 3000);
        check (bigAllocs == 0, "a block of 3000 after preparing for 512: processed in pieces, no allocation");

        // The oversampling switch: applied on the message thread once the audio thread is on the
        // current engine; the latency follows; no burst.
        set (*p, ParamID::oversample, 0.0f);
        p->updateOversampling();
        const int latency1x = p->getLatencySamples();
        juce::AudioBuffer<float> tone (2, 9600);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                tone.setSample (ch, i, (float) (0.25 * std::sin (2.0 * kPi * 1000.0 * i / 48000.0)));
        const long switchAllocs = run (*p, tone, 480);
        const float peak = tone.getMagnitude (0, 0, tone.getNumSamples());
        set (*p, ParamID::oversample, 1.0f);
        p->updateOversampling();
        check (latency1x == 0 && p->getActiveOversampling() == 2 && p->getLatencySamples() > 0 && peak < 0.3f && switchAllocs == 0,
               "2x -> 1x -> 2x: latency " + juce::String (latency1x) + " then " + juce::String (p->getLatencySamples()) + ", peak " + juce::String (peak, 3) + " for a 0.25 tone, no allocation in processBlock");
    }

    std::printf (failures == 0 ? "All checks passed.\n" : "%d check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
