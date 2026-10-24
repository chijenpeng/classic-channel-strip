#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace ar
{
/** Describes one control of a module. Knob values are in the given range (not normalised);
    choices are integers 0..n-1. */
struct ParamSpec
{
    enum class Type { knob, choice };

    std::string id;        // unique within the module, e.g. "gain"
    std::string name;      // shown on the panel, e.g. "Gain"
    Type type = Type::knob;
    float min = 0.0f, max = 10.0f, def = 5.0f;
    std::vector<std::string> choices;
    std::string help;      // shown while hovering
    bool asKnob = false;   // a choice drawn as a stepped knob rather than a row of words
    std::string unit;      // appended to a knob's value, e.g. "dB"
    bool logScale = false; // the knob sweeps min..max geometrically (frequencies, times)
    /** Choices drawn as a row of words: where each option sits on the slot's stored 0..1
        control, when not evenly spaced in order. Sessions and automation hold that value, so an
        option appended after sessions were saved takes a free place between the old ones,
        which keep theirs; reading picks the nearest place. Empty: option i of n at i / (n - 1). */
    std::vector<float> storedAt;
    /** Optional editor-only conditional visibility. The control is shown when the sibling
        parameter named by visibleWhen has one of visibleValues. DSP parameter identity and
        order never change; this only prevents mutually-exclusive control modes (Mix versus
        Dry/Wet, for example) from being drawn at the same time. */
    std::string visibleWhen;
    std::vector<int> visibleValues;
    /** A choice may act as a UI link switch for two sibling continuous controls. When its
        selected value is non-zero, moving either target moves the other by the same amount in
        the target's native units. */
    std::vector<std::string> linkedParameters;
    /** The saved-state version (rig::kStateVersion) that added this control to its module. A
        control added later is appended after the others, and a state saved before it existed
        holds something else in its slot control (the layout default, or another module's
        knob): such states load with the control at its default. Must be listed by params()
        before prepare(). */
    int since = 0;

    static ParamSpec knob (std::string id, std::string name, float def, std::string help, float min = 0.0f, float max = 10.0f)
    {
        ParamSpec spec;
        spec.id = std::move (id);
        spec.name = std::move (name);
        spec.min = min;
        spec.max = max;
        spec.def = def;
        spec.help = std::move (help);
        return spec;
    }

    /** A frequency knob in Hz with a logarithmic sweep (min > 0). */
    static ParamSpec frequency (std::string id, std::string name, float def, std::string help, float min, float max)
    {
        auto p = knob (std::move (id), std::move (name), def, std::move (help), min, max);
        p.unit = "Hz";
        p.logScale = true;
        return p;
    }

    static ParamSpec choice (std::string id, std::string name, std::vector<std::string> choices, int def, std::string help)
    {
        ParamSpec spec;
        spec.id = std::move (id);
        spec.name = std::move (name);
        spec.type = Type::choice;
        spec.max = (float) (choices.size() - 1);
        spec.def = (float) def;
        spec.choices = std::move (choices);
        spec.help = std::move (help);
        return spec;
    }

    static ParamSpec steppedKnob (std::string id, std::string name, std::vector<std::string> choices, int def, std::string help)
    {
        auto spec = choice (std::move (id), std::move (name), std::move (choices), def, std::move (help));
        spec.asKnob = true;
        return spec;
    }

    ParamSpec& shownWhen (std::string siblingId, std::initializer_list<int> values)
    {
        visibleWhen = std::move (siblingId);
        visibleValues.assign (values.begin(), values.end());
        return *this;
    }

    ParamSpec& links (std::string first, std::string second)
    {
        linkedParameters = { std::move (first), std::move (second) };
        return *this;
    }
};

/** The rig's global latency mode. Tracking is for playing: the lowest latency, minimum-phase
    filters, time-domain pitch shifting. Mixing allows latency for quality: linear-phase
    filters, a phase vocoder. Switching it rebuilds every module. */
enum class LatencyMode { tracking, mixing };

/** One selectable sound: an amp channel, or later a pedal. Mono, processed at the internal
    (oversampled) rate. Thread-safety: setParam() and process() are called from the audio
    thread; everything else is set-up. */
class Module
{
public:
    virtual ~Module() = default;

    virtual std::string id() const = 0;          // e.g. "recto_mw_ch3"
    virtual std::string family() const = 0;      // e.g. "Dual Rectifier Multi-Watt"
    virtual std::string name() const = 0;        // e.g. "Channel 3"
    /** Longer browser copy. Empty means the editor falls back to the displayed module name. */
    virtual std::string description() const { return {}; }
    /** Legacy ids may stay loadable for old sessions without appearing as choices for new rigs. */
    virtual bool shownInBrowser() const { return true; }
    /** Bass amps and bass pedals (the editor colours them apart from the guitar ones). */
    virtual bool forBass() const { return false; }
    virtual std::vector<ParamSpec> params() const = 0;

    /** Builds the circuit for this rate. Expensive; not for the audio thread. */
    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    /** Index refers to params(); value in the spec's range. Takes effect smoothly. */
    virtual void setParam (int index, float value) = 0;
    /** In-place; samples are volts at the amp input and volts at the speaker on return. */
    virtual void process (float* samples, int numSamples) = 0;

    /** Output scaling so that full power comes out around +0 dBFS. */
    virtual float outputVoltsForFullScale() const { return 60.0f; }
    /** Called before prepare() with the rig's latency mode. Blocks that have a low-latency and
        a high-quality way of working (pitch shifting, digital filters) pick one here, so the
        user sets it once for the whole rig; latencySamples() reports the result. */
    virtual void setLatencyMode (LatencyMode) {}
    /** Effects (filters, dynamics, pitch, time, cabinets) work on the rig's full-scale signal,
        1.0 = 0 dBFS, wherever they sit: after an amp (tens of volts) the rig rescales before
        them. Circuit modules (amps, pedals) work in volts. */
    virtual bool fullScaleInput() const { return false; }
    /** Full-scale blocks that model something physical in volts (a cabinet's voice-coil heating)
        are told, before process(), how many volts the full scale stands for: the last circuit
        block's outputVoltsForFullScale(), even with other full-scale effects in between. */
    virtual void setInputVoltsForFullScale (float) noexcept {}
    /** Delay this block adds (look-ahead, pitch shifting), in samples at the rate prepare()
        was given. The rig compensates the other lane and reports the total to the host. */
    virtual int latencySamples() const { return 0; }
    /** How long the block keeps sounding after its input stops (reverb, delay), in seconds. */
    virtual double tailSeconds() const { return 0.0; }
    /** Reverbs and delays, whose tail is part of the sound. The rig's noise gate acts just
        before the first of them in a lane (as a gate in the amp's loop with the time effects
        after it), so it never cuts their tails. Not every block with a tail: a cabinet or a
        linear-phase filter rings briefly but is no time effect. */
    virtual bool isTimeEffect() const { return false; }
    /** The host's tempo in beats per minute, before every process() call on the audio thread;
        0 when the host does not provide a play head. */
    virtual void setHostTempo (double /*bpm*/) noexcept {}
    /** Time effects run at the host rate, outside the oversampled section: oversampling them
        only costs CPU and memory. prepare() then gets the host rate. */
    virtual bool runsAtHostRate() const { return false; }
    /** Stereo blocks get both channels through processStereo() (a mono source arrives as two
        identical channels) and may make them differ (reverb, Haas width). Others run once per
        channel through process(). */
    virtual bool isStereo() const { return false; }
    virtual void processStereo (float* left, float* right, int numSamples) { process (left, numSamples); process (right, numSamples); }
    /** Time effects can send only their wet signal to the rig's output return. The dry signal
        keeps travelling through later blocks while wetLeft / wetRight bypass them. The default
        implementation is deliberately inert so ordinary modules need no special routing code. */
    virtual bool routesWetOut() const { return false; }
    virtual void processStereoWithWetOut (float* left, float* right, float* wetLeft, float* wetRight, int numSamples)
    {
        processStereo (left, right, numSamples);
        std::fill (wetLeft, wetLeft + numSamples, 0.0f);
        std::fill (wetRight, wetRight + numSamples, 0.0f);
    }
    /** Compressors: the panel shows a gain reduction meter. */
    virtual bool hasGainReductionMeter() const { return false; }
    /** The largest gain reduction (dB, 0 or more) during the last process() call. */
    virtual float gainReductionDb() const { return 0.0f; }
    /** Modules with a clipper (the SSL card): the largest voltage that reached it above its linear
        limit since the last call, in volts at the module (0 when it stayed linear). Reading
        resets it. Lock-free; any thread. */
    virtual float takeOverloadVolts() noexcept { return 0.0f; }
};

using ModulePtr = std::unique_ptr<Module>;
} // namespace ar
