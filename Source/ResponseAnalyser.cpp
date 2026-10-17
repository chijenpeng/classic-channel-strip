#include "ResponseAnalyser.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <map>
#include <memory>
#include <vector>

ResponseAnalyser::ResponseAnalyser() : juce::Thread ("SSL response")
{
    startThread (juce::Thread::Priority::low);
}

ResponseAnalyser::~ResponseAnalyser()
{
    // The analysis checks for this between 512-sample blocks, so it stops within a few ms.
    stopThread (4000);
}

int ResponseAnalyser::request (const Settings& settings, double sampleRate)
{
    int generation = 0;
    {
        const juce::SpinLock::ScopedLockType lock (requestLock);   // held for a copy, never for an analysis
        requested = settings;
        requestedRate = sampleRate;
        generation = ++requestedGeneration;
        pending = true;
    }
    notify();
    return generation;
}

bool ResponseAnalyser::fetch (Curve& out)
{
    if (! resultReady.load())
        return false;
    const juce::SpinLock::ScopedTryLockType lock (resultLock);
    if (! lock.isLocked())
        return false;
    out = result;
    resultReady.store (false);
    return true;
}

double ResponseAnalyser::frequencyAt (int i) noexcept
{
    return kLowHz * std::pow (kHighHz / kLowHz, (double) i / (kPoints - 1));
}

float ResponseAnalyser::dbAt (const Curve& curve, double hz) noexcept
{
    const double position = std::log (juce::jlimit (kLowHz, kHighHz, hz) / kLowHz) / std::log (kHighHz / kLowHz) * (kPoints - 1);
    const int i = juce::jlimit (0, kPoints - 2, (int) std::floor (position));
    const double t = position - i;
    return (float) ((1.0 - t) * curve.db[(size_t) i] + t * curve.db[(size_t) i + 1]);
}

void ResponseAnalyser::run()
{
    ar::ModulePtr card;
    double cardRate = 0.0;
    std::map<int, std::unique_ptr<juce::dsp::FFT>> ffts;   // by order
    std::vector<float> data, binDb;
    double lastStart = -1.0e9;

    while (! threadShouldExit())
    {
        bool any = false;
        {
            const juce::SpinLock::ScopedLockType lock (requestLock);
            any = pending;
        }
        if (! any)
        {
            wait (-1);
            continue;
        }
        // Coalesce: wait out the interval; whatever is latest then is what gets analysed.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastStart < kMinIntervalMs)
        {
            wait (juce::jmax (1, (int) std::ceil (kMinIntervalMs - (now - lastStart))));
            continue;
        }
        lastStart = now;

        Settings settings;
        double rate = 0.0;
        int generation = 0;
        {
            const juce::SpinLock::ScopedLockType lock (requestLock);
            settings = requested;
            rate = requestedRate;
            generation = requestedGeneration;
            pending = false;
        }
        if (rate <= 0.0)
            continue;

        if (card == nullptr || ! juce::exactlyEqual (rate, cardRate))
        {
            card = ar::fx::makeSslE242();
            card->prepare (rate);
            cardRate = rate;
        }
        for (int i = 0; i < kNumControls; ++i)
            card->setParam (i, settings[(size_t) i]);
        card->reset();

        // The impulse response: 16384 samples up to 96 kHz, longer in proportion above (the
        // same 0.17 s). Its FFT zero-padded to 4x the length: the same spectrum, sampled finely
        // enough that a narrow LF bell falls between no bins (every 1.5 Hz at 96 kHz).
        int lengthOrder = kLengthOrder;
        while (rate / (1 << lengthOrder) > 96000.0 / (1 << kLengthOrder) + 1.0e-9)
            ++lengthOrder;
        const int length = 1 << lengthOrder;
        const int order = lengthOrder + kZeroPadOrder, size = 1 << order;
        auto& fft = ffts[order];
        if (fft == nullptr)
            fft = std::make_unique<juce::dsp::FFT> (order);

        data.assign ((size_t) size * 2, 0.0f);
        data[0] = kImpulse;
        for (int start = 0; start < length; start += 512)
        {
            if (threadShouldExit())
                return;
            card->process (data.data() + start, juce::jmin (512, length - start));
        }
        for (int n = 0; n < length; ++n)
            data[(size_t) n] /= kImpulse;
        fft->performFrequencyOnlyForwardTransform (data.data(), true);

        binDb.resize ((size_t) size / 2 + 1);
        for (size_t k = 0; k < binDb.size(); ++k)
            binDb[k] = 20.0f * std::log10 (juce::jmax (data[k], 1.0e-9f));

        // Onto the grid: between two bins, linear in dB against log frequency (slopes are
        // straight lines there); between DC and the first bin, linear in frequency.
        Curve curve;
        curve.generation = generation;
        curve.sampleRate = rate;
        const double binHz = rate / size;
        for (int i = 0; i < kPoints; ++i)
        {
            const double hz = juce::jmin (frequencyAt (i), 0.5 * rate);
            const double position = hz / binHz;
            const int k0 = juce::jlimit (0, (int) binDb.size() - 2, (int) std::floor (position));
            const double t = k0 == 0 ? position - k0 : std::log (hz / (k0 * binHz)) / std::log ((k0 + 1.0) / k0);
            curve.db[(size_t) i] = (float) ((1.0 - t) * binDb[(size_t) k0] + t * binDb[(size_t) k0 + 1]);
        }

        {
            const juce::SpinLock::ScopedLockType lock (resultLock);
            result = curve;
        }
        resultReady.store (true);
    }
}
