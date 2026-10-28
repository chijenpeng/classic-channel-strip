#pragma once

#include "dsp/SslE242.h"
#include "dsp/SslE10Dynamics.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

/** Parameter ids: stable, sessions and automation store them. In the host's order. */
namespace ParamID
{
    inline constexpr auto input = "input", hpfOn = "hpf_on", hpfFreq = "hpf_freq", lpfOn = "lpf_on", lpfFreq = "lpf_freq",
                          hfGain = "hf_gain", hfFreq = "hf_freq", hfBell = "hf_bell",
                          hmfGain = "hmf_gain", hmfFreq = "hmf_freq", hmfQ = "hmf_q",
                          lmfGain = "lmf_gain", lmfFreq = "lmf_freq", lmfQ = "lmf_q",
                          lfGain = "lf_gain", lfFreq = "lf_freq", lfBell = "lf_bell",
                          eqOn = "eq_on", output = "output", oversample = "oversample",
                          dynOn = "dyn_on", compThreshold = "comp_threshold", compRatio = "comp_ratio",
                          compRelease = "comp_release", compFast = "comp_fast", compMakeup = "comp_makeup",
                          gateThreshold = "gate_threshold", gateRange = "gate_range", gateRelease = "gate_release",
                          gateMode = "gate_mode", gateFast = "gate_fast", dynPosition = "dyn_position",
                          sidechainSource = "sidechain_source";
}

/** The SSL 4000 E channel strip: 82E242 EQ/filter circuit model plus the stereo-linked 82E10
    T82010.71 dynamics circuit, optional external key, input/output trims, at host rate or 2x oversampled.

    Threads. The audio thread never allocates or locks: both oversampling factors are built in
    prepareToPlay(), and a change of the Oversampling parameter is applied on the message thread
    (a timer), which resets the idle engine and then hands it to the audio thread through an
    atomic index, and reports the new latency. */
class Ssl4000EqProcessor final : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    Ssl4000EqProcessor();
    ~Ssl4000EqProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== For the editor ==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** The largest input sample (absolute, 1.0 = 0 dBFS) on a channel since the last call, before
        the Input trim, then starts over. Lock-free; for meters. */
    float takeInputPeak (int channel) noexcept;
    /** The largest output sample (absolute, 1.0 = 0 dBFS) on a channel since the last call, then
        starts over. Lock-free; for meters. */
    float takeOutputPeak (int channel) noexcept;
    /** The compressor's gain reduction over the last host block, in positive dB. It is held, not
        taken: several reads between two blocks see the same value (the editor's timer reads it
        at 30 Hz). Lock-free; for the GR meter. */
    float compressorReductionDb() const noexcept { return compressorReduction.load (std::memory_order_relaxed); }
    /** The largest level that reached the card's op-amp clippers (the section outputs) above their
        linear limit (+-14.85 V = +4.6 dBFS at the card) since the last call, in dBFS at the card
        (after the Input trim, the filters' and bands' gain included); -120 when the card stayed
        linear. Lock-free; for a clip indicator. Does not affect the audio. */
    float takeCardOverload() noexcept;
    /** The oversampling factor the audio thread runs (1 or 2). */
    int getActiveOversampling() const noexcept { return requested.load() == 1 ? 2 : 1; }

    /** Applies a change of the Oversampling parameter: message thread only (the timer calls it;
        offline tests may call it directly). */
    void updateOversampling();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    static constexpr int kModuleParams = (int) ar::fx::kSslParamIds.size();

    /** One oversampling factor: an audio card, two sidechain colouring paths and linked dynamics. */
    struct Engine
    {
        int factor = 1;
        std::array<ar::ModulePtr, 2> cards;
        std::array<ar::ModulePtr, 2> sidechainFilters, sidechainEq;
        ar::fx::SslE10Dynamics dynamics;
        std::array<float, kModuleParams> sent {};   // NaN: not sent yet
    };

    void timerCallback() override { updateOversampling(); }
    void prepareEngines (double sampleRate, int samplesPerBlock);
    void setModuleParameter (Engine&, int index, float value) noexcept;
    void pushParameters (Engine&) noexcept;
    void resetEngine (int index);
    int latencyOf (int index) const noexcept;
    int desiredEngine() const noexcept { return oversampleParam->load() > 0.5f ? 1 : 0; }

    std::array<std::atomic<float>*, kModuleParams> moduleParams {};
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* oversampleParam = nullptr;
    std::atomic<float>* dynOnParam = nullptr;
    std::atomic<float>* compThresholdParam = nullptr;
    std::atomic<float>* compRatioParam = nullptr;
    std::atomic<float>* compReleaseParam = nullptr;
    std::atomic<float>* compFastParam = nullptr;
    std::atomic<float>* compMakeupParam = nullptr;
    std::atomic<float>* dynPositionParam = nullptr;
    std::atomic<float>* sidechainSourceParam = nullptr;

    std::array<Engine, 2> engines;                              // [0] 1x, [1] 2x
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler; // for engines[1]
    std::atomic<int> requested { 1 };      // the engine the audio thread is to use
    std::atomic<int> acknowledged { 1 };   // the engine the audio thread last started a block with
    juce::CriticalSection preparing;       // prepareToPlay against the timer (never the audio thread)
    bool prepared = false;
    int preparedChannels = 0, maxBlock = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain, outputGain;
    std::vector<float> gainScratch;
    std::array<std::vector<float>, 2> sidechainScratch;
    std::array<std::atomic<float>, 2> inputPeak {}, outputPeak {};
    std::atomic<float> compressorReduction { 0.0f };
    std::atomic<float> cardOverloadVolts { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ssl4000EqProcessor)
};
