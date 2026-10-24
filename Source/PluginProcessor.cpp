#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>

namespace
{
/** A frequency or Q knob that sweeps geometrically, as the module's logScale controls do. */
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    // In double, so that a value survives the round trip through 0..1 (hosts check defaults).
    return { lo, hi,
             [] (float start, float end, float x) { return (float) ((double) start * std::pow ((double) end / start, (double) x)); },
             [] (float start, float end, float v) { return (float) (std::log ((double) v / start) / std::log ((double) end / start)); },
             [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
}

juce::String hzText (float v, int)
{
    if (v >= 1000.0f)
        return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
}

float hzValue (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    const float v = t.getFloatValue();
    return t.containsChar ('k') ? v * 1000.0f : v;
}

juce::String dbText (float v, int)
{
    return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
}

juce::String secondsText (float v, int)
{
    return juce::String (v, 2) + " s";
}

juce::String ratioText (float v, int)
{
    return v >= 19.5f ? "LIMIT" : juce::String (v, 1) + ":1";
}

float numberValue (const juce::String& text)
{
    return text.getFloatValue();
}

/** The text of a knob in `unit`: Hz / kHz, dB with its sign, or a plain number (Q). */
juce::AudioParameterFloatAttributes attributesFor (const std::string& unit)
{
    auto a = juce::AudioParameterFloatAttributes().withLabel (unit);
    if (unit == "Hz")
        return a.withStringFromValueFunction (hzText).withValueFromStringFunction (hzValue);
    if (unit == "dB")
        return a.withStringFromValueFunction (dbText).withValueFromStringFunction (numberValue);
    return a.withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })
            .withValueFromStringFunction (numberValue);
}

std::unique_ptr<juce::RangedAudioParameter> trimParameter (const char* id, const char* name)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (-20.0f, 20.0f), 0.0f,
                                                        attributesFor ("dB"));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout Ssl4000EqProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (trimParameter (ParamID::input, "Input"));

    // The card's own controls, ranges and defaults (HPF, LPF, HF, HMF, LMF, LF, EQ in).
    for (const auto& s : ar::fx::sslE242Params())
    {
        const juce::ParameterID id { juce::String (s.id), 1 };
        if (s.type == ar::ParamSpec::Type::choice)
        {
            juce::StringArray choices;
            for (const auto& c : s.choices) choices.add (c);
            layout.add (std::make_unique<juce::AudioParameterChoice> (id, s.name, choices, (int) s.def));
        }
        else
        {
            const auto range = s.logScale ? logRange (s.min, s.max) : juce::NormalisableRange<float> (s.min, s.max);
            layout.add (std::make_unique<juce::AudioParameterFloat> (id, s.name, range, s.def, attributesFor (s.unit)));
        }
    }

    layout.add (trimParameter (ParamID::output, "Output"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::oversample, 1 }, "Oversampling",
                                                              juce::StringArray { "1x", "2x" }, 1));
    // Appended after the original 20 parameters: old sessions and automation keep their order.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::dynOn, 1 }, "Dynamics",
                                                              juce::StringArray { "Out", "In" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamID::compThreshold, 1 }, "Comp threshold",
                                                             juce::NormalisableRange<float> (-20.0f, 10.0f), 10.0f, attributesFor ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamID::compRatio, 1 }, "Comp ratio",
                                                             logRange (1.0f, 20.0f), 2.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (ratioText).withValueFromStringFunction (numberValue)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamID::compRelease, 1 }, "Comp release",
                                                             logRange (0.1f, 4.0f), 0.4f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction (secondsText).withValueFromStringFunction (numberValue)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::compFast, 1 }, "Comp attack",
                                                              juce::StringArray { "Auto", "Fast" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::compMakeup, 1 }, "Comp make-up",
                                                              juce::StringArray { "Off", "Auto" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamID::gateThreshold, 1 }, "Gate threshold",
                                                             juce::NormalisableRange<float> (-30.0f, 10.0f), -30.0f, attributesFor ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamID::gateRange, 1 }, "Gate range",
                                                             juce::NormalisableRange<float> (0.0f, 40.0f), 0.0f, attributesFor ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamID::gateRelease, 1 }, "Gate release",
                                                             logRange (0.1f, 4.0f), 0.4f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction (secondsText).withValueFromStringFunction (numberValue)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::gateMode, 1 }, "Gate mode",
                                                              juce::StringArray { "Gate", "Expand" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::gateFast, 1 }, "Gate attack",
                                                              juce::StringArray { "Slow", "Fast" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::dynPosition, 1 }, "Dynamics position",
                                                              juce::StringArray { "Pre EQ", "Post EQ" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::sidechainSource, 1 }, "Sidechain",
                                                              juce::StringArray { "Input", "Filters", "EQ", "External" }, 0));
    return layout;
}

Ssl4000EqProcessor::Ssl4000EqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SSL4000EQ", createLayout())
{
    for (size_t i = 0; i < moduleParams.size(); ++i)
        moduleParams[i] = apvts.getRawParameterValue (ar::fx::kSslParamIds[i]);
    inputParam = apvts.getRawParameterValue (ParamID::input);
    outputParam = apvts.getRawParameterValue (ParamID::output);
    oversampleParam = apvts.getRawParameterValue (ParamID::oversample);
    dynOnParam = apvts.getRawParameterValue (ParamID::dynOn);
    compThresholdParam = apvts.getRawParameterValue (ParamID::compThreshold);
    compRatioParam = apvts.getRawParameterValue (ParamID::compRatio);
    compReleaseParam = apvts.getRawParameterValue (ParamID::compRelease);
    compFastParam = apvts.getRawParameterValue (ParamID::compFast);
    compMakeupParam = apvts.getRawParameterValue (ParamID::compMakeup);
    dynPositionParam = apvts.getRawParameterValue (ParamID::dynPosition);
    sidechainSourceParam = apvts.getRawParameterValue (ParamID::sidechainSource);
    engines[0].factor = 1;
    engines[1].factor = 2;
    startTimerHz (20);
}

Ssl4000EqProcessor::~Ssl4000EqProcessor()
{
    stopTimer();
}

bool Ssl4000EqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if ((out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        || out != layouts.getMainInputChannelSet())
        return false;
    if (layouts.inputBuses.size() < 2 || layouts.inputBuses[1].isDisabled())
        return true;
    return layouts.inputBuses[1] == juce::AudioChannelSet::mono() || layouts.inputBuses[1] == juce::AudioChannelSet::stereo();
}

void Ssl4000EqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    {
        const juce::ScopedLock lock (preparing);
        prepareEngines (sampleRate, samplesPerBlock);
    }
    setLatencySamples (latencyOf (requested.load()));   // outside the lock: hosts may call back
}

void Ssl4000EqProcessor::prepareEngines (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, 2, juce::jmax (getMainBusNumInputChannels(), getMainBusNumOutputChannels()));
    maxBlock = juce::jmax (1, samplesPerBlock);

    // Both factors, ready to run: switching between them never builds on the audio thread. A stereo
    // instance has one card (and one key-path card per section) holding both channels, which share
    // the state-space matrices; a mono one has a one-channel card.
    for (auto& e : engines)
    {
        const auto fill = [&] (std::array<ar::ModulePtr, 2>& cards, ar::fx::SslSections sections)
        {
            cards[0] = ar::fx::makeSslE242 (sections, preparedChannels);
            cards[0]->prepare (sampleRate * e.factor);
            cards[1].reset();
        };
        fill (e.cards, ar::fx::SslSections::all);
        fill (e.sidechainFilters, ar::fx::SslSections::filtersOnly);
        fill (e.sidechainEq, ar::fx::SslSections::eqOnly);
        e.dynamics.prepare (sampleRate * e.factor);
    }
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) preparedChannels, 1,
                                                                    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlock);

    const int index = desiredEngine();
    resetEngine (0);
    resetEngine (1);
    requested.store (index);
    acknowledged.store (index);

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputParam->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    gainScratch.assign ((size_t) maxBlock, 1.0f);
    for (auto& scratch : sidechainScratch)
        scratch.assign ((size_t) maxBlock * 2, 0.0f);
    for (auto& p : inputPeak) p.store (0.0f);
    for (auto& p : outputPeak) p.store (0.0f);

    prepared = true;
}

int Ssl4000EqProcessor::latencyOf (int index) const noexcept
{
    return index == 1 && oversampler != nullptr ? juce::roundToInt (oversampler->getLatencyInSamples()) : 0;
}

void Ssl4000EqProcessor::resetEngine (int index)
{
    // The engine is idle here (prepareToPlay, or the timer once the audio thread has moved to
    // the other one): bring it to the current settings and its operating point.
    auto& e = engines[(size_t) index];
    for (int i = 0; i < kModuleParams; ++i)
        setModuleParameter (e, i, moduleParams[(size_t) i]->load());
    e.cards[0]->reset();
    e.sidechainFilters[0]->reset();
    e.sidechainEq[0]->reset();
    e.dynamics.reset();
    e.sent.fill (std::numeric_limits<float>::quiet_NaN());   // pushParameters() sends everything again
    if (index == 1 && oversampler != nullptr)
        oversampler->reset();
}

void Ssl4000EqProcessor::setModuleParameter (Engine& e, int index, float value) noexcept
{
    e.cards[0]->setParam (index, value);

    if (index < ar::fx::kSslFilterParamCount)
        e.sidechainFilters[0]->setParam (index, value);
    else if (index < ar::fx::kSslEqEnabledParamIndex)
        e.sidechainEq[0]->setParam (index - ar::fx::kSslEqParamOffset, value);
}

void Ssl4000EqProcessor::updateOversampling()
{
    int latency = -1;
    {
        const juce::ScopedLock lock (preparing);
        const int want = desiredEngine(), current = requested.load();
        // Only once the audio thread has started a block with the current engine is the other
        // one certainly idle.
        if (! prepared || want == current || acknowledged.load() != current)
            return;
        resetEngine (want);
        requested.store (want);
        latency = latencyOf (want);
    }
    setLatencySamples (latency);   // outside the lock: hosts may call back
}

void Ssl4000EqProcessor::pushParameters (Engine& e) noexcept
{
    for (int i = 0; i < kModuleParams; ++i)
    {
        const float v = moduleParams[(size_t) i]->load (std::memory_order_relaxed);
        if (! juce::exactlyEqual (v, e.sent[(size_t) i]))   // NaN: never sent
        {
            setModuleParameter (e, i, v);
            e.sent[(size_t) i] = v;
        }
    }
}

void Ssl4000EqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getMainBusNumInputChannels(), numOut = getMainBusNumOutputChannels(), n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
    if (! prepared)
        return;
    const int channels = juce::jmin (numIn, numOut, preparedChannels);

    // Meters: the largest sample in this block, kept until the editor takes it.
    const auto holdPeaks = [&buffer, channels, n] (std::array<std::atomic<float>, 2>& peaks)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            const float peak = buffer.getMagnitude (ch, 0, n);
            auto& held = peaks[(size_t) ch];
            for (float old = held.load (std::memory_order_relaxed); peak > old && ! held.compare_exchange_weak (old, peak);) {}
        }
    };
    holdPeaks (inputPeak);   // before the Input trim

    const int index = requested.load (std::memory_order_acquire);
    acknowledged.store (index, std::memory_order_release);
    auto& engine = engines[(size_t) index];
    pushParameters (engine);
    ar::fx::SslE10Settings dynamicsSettings;
    dynamicsSettings.enabled = dynOnParam->load (std::memory_order_relaxed) > 0.5f;
    dynamicsSettings.compressorThresholdDb = compThresholdParam->load (std::memory_order_relaxed);
    dynamicsSettings.compressorRatio = compRatioParam->load (std::memory_order_relaxed);
    dynamicsSettings.compressorReleaseSeconds = compReleaseParam->load (std::memory_order_relaxed);
    dynamicsSettings.compressorFastAttack = compFastParam->load (std::memory_order_relaxed) > 0.5f;
    dynamicsSettings.automaticMakeup = compMakeupParam->load (std::memory_order_relaxed) > 0.5f;
    // Keep the old parameter IDs for session compatibility, but never send gate attenuation
    // to the VCA. This also bypasses nonzero gate settings restored from older sessions.
    dynamicsSettings.gateRangeDb = 0.0f;
    engine.dynamics.setSettings (dynamicsSettings);
    const int sidechainChoice = juce::roundToInt (sidechainSourceParam->load (std::memory_order_relaxed));
    const bool postEq = dynPositionParam->load (std::memory_order_relaxed) > 0.5f;
    const bool externalAvailable = getBusCount (true) > 1 && getBus (true, 1)->isEnabled();
    auto sidechainBus = externalAvailable ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputParam->load (std::memory_order_relaxed)));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load (std::memory_order_relaxed)));
    const auto applyGain = [&] (juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>& gain, int start, int len)
    {
        if (! gain.isSmoothing())
        {
            const float g = gain.getTargetValue();
            if (! juce::exactlyEqual (g, 1.0f))
                for (int ch = 0; ch < channels; ++ch)
                    juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch, start), g, len);
            return;
        }
        for (int i = 0; i < len; ++i)
            gainScratch[(size_t) i] = gain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch, start), gainScratch.data(), len);
    };

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        applyGain (inputGain, start, len);
        const auto processAtRate = [&] (float* left, float* right, int internalLength, int factor)
        {
            const auto makeSidechain = [&] (float* sourceLeft, float* sourceRight)
            {
                float* scLeft = sourceLeft;
                float* scRight = sourceRight;
                const bool useExternal = sidechainChoice == 3 && externalAvailable && sidechainBus.getNumChannels() > 0;
                if (useExternal || sidechainChoice == 1 || sidechainChoice == 2)
                {
                    scLeft = sidechainScratch[0].data();
                    scRight = channels > 1 ? sidechainScratch[1].data() : nullptr;
                    for (int i = 0; i < internalLength; ++i)
                    {
                        if (useExternal)
                        {
                            const int hostIndex = start + juce::jmin (len - 1, i / factor);
                            scLeft[i] = sidechainBus.getSample (0, hostIndex);
                            if (scRight != nullptr)
                                scRight[i] = sidechainBus.getSample (juce::jmin (1, sidechainBus.getNumChannels() - 1), hostIndex);
                        }
                        else
                        {
                            scLeft[i] = sourceLeft[i];
                            if (scRight != nullptr) scRight[i] = sourceRight[i];
                        }
                    }
                    const auto runKey = [&] (ar::Module& card)
                    {
                        if (channels > 1) card.processStereo (sidechainScratch[0].data(), sidechainScratch[1].data(), internalLength);
                        else              card.process (sidechainScratch[0].data(), internalLength);
                    };
                    if (sidechainChoice == 1)
                        runKey (*engine.sidechainFilters[0]);
                    else if (sidechainChoice == 2 && moduleParams[16]->load (std::memory_order_relaxed) > 0.5f)
                        runKey (*engine.sidechainEq[0]);
                }
                engine.dynamics.process (left, right, scLeft, scRight, internalLength);
            };

            if (! postEq)
                makeSidechain (left, right);
            if (right != nullptr) engine.cards[0]->processStereo (left, right, internalLength);
            else                  engine.cards[0]->process (left, internalLength);
            if (postEq)
                makeSidechain (left, right);
        };

        if (engine.factor == 1)
            processAtRate (buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr, len, 1);
        else
        {
            auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) channels).getSubBlock ((size_t) start, (size_t) len);
            auto up = oversampler->processSamplesUp (block);
            processAtRate (up.getChannelPointer (0), channels > 1 ? up.getChannelPointer (1) : nullptr, (int) up.getNumSamples(), 2);
            oversampler->processSamplesDown (block);
        }
        applyGain (outputGain, start, len);
    }

    const auto holdReduction = [] (std::atomic<float>& held, float value)
    {
        for (float old = held.load (std::memory_order_relaxed); value > old && ! held.compare_exchange_weak (old, value);) {}
    };
    holdReduction (compressorReduction, engine.dynamics.compressorGainReductionDb());
    holdReduction (cardOverloadVolts, engine.cards[0]->takeOverloadVolts());

    holdPeaks (outputPeak);
}

float Ssl4000EqProcessor::takeCardOverload() noexcept
{
    const float volts = cardOverloadVolts.exchange (0.0f);
    return volts > 0.0f ? juce::Decibels::gainToDecibels (volts / ar::fx::kSslVoltsPerFullScale) : -120.0f;
}

float Ssl4000EqProcessor::takeInputPeak (int channel) noexcept
{
    return juce::isPositiveAndBelow (channel, 2) ? inputPeak[(size_t) channel].exchange (0.0f) : 0.0f;
}

float Ssl4000EqProcessor::takeOutputPeak (int channel) noexcept
{
    return juce::isPositiveAndBelow (channel, 2) ? outputPeak[(size_t) channel].exchange (0.0f) : 0.0f;
}

void Ssl4000EqProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Ssl4000EqProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto restored = juce::ValueTree::fromXml (*xml);
        // Sessions from the EQ-only release contain the original 20 properties. Add defaults for
        // controls introduced with 82E10 instead of inheriting whatever the current instance had.
        for (auto* parameter : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            {
                const auto id = ranged->getParameterID();
                bool found = false;
                for (int i = 0; i < restored.getNumChildren(); ++i)
                    found = found || restored.getChild (i).getProperty ("id").toString() == id;
                if (! found)
                {
                    juce::ValueTree value ("PARAM");
                    value.setProperty ("id", id, nullptr);
                    value.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                    restored.appendChild (value, nullptr);
                }
            }
        apvts.replaceState (restored);
    }
}

juce::AudioProcessorEditor* Ssl4000EqProcessor::createEditor()
{
    return new Ssl4000EqEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Ssl4000EqProcessor();
}
