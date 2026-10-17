#pragma once

#include "dsp/SslE242.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

/** The magnitude response of the card at given settings, measured from the model itself: a
    shadow mono card (ar::fx::makeSslE242()) on a background thread, set to the settings, reset,
    an impulse of 0.1 run through it, the impulse response's FFT in dB, resampled onto a log
    frequency grid from 20 Hz to 20 kHz.

    The impulse response is 16384 samples long at internal rates up to 96 kHz (longer in
    proportion above: the same 0.17 s). Its FFT is zero-padded to 65536 points: a 16384-point FFT
    at 96 kHz (a bin every 5.9 Hz) misses the top of a narrow LF bell at 37 Hz by about 0.5 dB,
    while the padded one (every 1.5 Hz) is the same spectrum sampled finely enough. Between bins
    the dB values are interpolated on log frequency.

    Threads. request() and fetch() are for the message thread and never wait for the analysis:
    request() stores the latest settings (a newer request replaces one not started yet) and wakes
    the thread; fetch() takes the newest finished curve if there is one. The thread starts an
    analysis at most every 50 ms (about 30 ms of work each), so moving a knob gives up to 20
    curves a second, each for the settings current when it started. Nothing here touches the
    plug-in's audio thread or its cards. */
class ResponseAnalyser final : private juce::Thread
{
public:
    static constexpr int kNumControls = (int) ar::fx::kSslParamIds.size();
    static constexpr int kPoints = 1024;
    static constexpr double kLowHz = 20.0, kHighHz = 20000.0;
    static constexpr int kMinIntervalMs = 50;
    static constexpr float kImpulse = 0.1f;
    static constexpr int kLengthOrder = 14;    // 16384 samples of impulse response (up to 96 kHz)
    static constexpr int kZeroPadOrder = 2;    // FFT 4x as long: 65536 points

    /** The card's controls in kSslParamIds order, in their own units (Hz, dB, Q, choice index). */
    using Settings = std::array<float, kNumControls>;

    struct Curve
    {
        std::array<float, kPoints> db {};   // at frequencyAt (i)
        int generation = 0;                 // of the request it answers; 0: none yet
        double sampleRate = 0.0;            // the card's rate
    };

    ResponseAnalyser();
    ~ResponseAnalyser() override;

    /** Asks for the response at `settings` with the card at `sampleRate`. Returns the request's
        generation (increasing). Message thread. */
    int request (const Settings& settings, double sampleRate);

    /** Copies the newest finished curve into `out` if there is one that was not fetched yet.
        Message thread; never waits (if the thread is publishing just then, try again later). */
    bool fetch (Curve& out);

    /** The frequency of grid point `i`, log-spaced from 20 Hz to 20 kHz. */
    static double frequencyAt (int i) noexcept;
    /** A curve's value at any frequency in 20 Hz - 20 kHz (linear on log frequency between grid
        points). */
    static float dbAt (const Curve&, double hz) noexcept;

private:
    void run() override;

    juce::SpinLock requestLock, resultLock;
    Settings requested {};
    double requestedRate = 0.0;
    int requestedGeneration = 0;
    bool pending = false;

    Curve result;
    std::atomic<bool> resultReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseAnalyser)
};
