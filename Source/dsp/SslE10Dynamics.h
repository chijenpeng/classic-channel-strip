#pragma once

#include <atomic>

namespace ar::fx
{
/** Control surface of the 82E10 dynamics card. The controls keep the console's dBu legends;
    the plug-in uses the same 0 dBFS = +18 dBu alignment as the 82E242 card. */
struct SslE10Settings
{
    bool enabled = false;
    float compressorThresholdDb = 10.0f;
    float compressorRatio = 2.0f;       // 20 means LIMIT
    float compressorReleaseSeconds = 0.1f;
    bool compressorFastAttack = false;
    bool automaticMakeup = true;
    float gateThresholdDb = -30.0f;
    float gateRangeDb = 0.0f;
    float gateReleaseSeconds = 0.4f;
    bool expander = false;
    bool gateFastAttack = false;
};

/** T82010.71 issue-15 schematic model. The AD536J detector and DBX 2151 are datasheet
    macromodels (the two ICs are blocks on the drawing); their surrounding resistors, diodes,
    timing capacitors, pots, switches, VCA I/O network and 5534 output stage follow the drawing.
    A linked pair shares the dominant control voltage as the console's LINK connection does.
    It owns no buffers and never allocates in process(). */
class SslE10Dynamics
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setSettings (const SslE10Settings& newSettings) noexcept { settings = newSettings; }

    /** sideLeft/right may alias audioLeft/right. right may be null for mono. */
    void process (float* audioLeft, float* audioRight, const float* sideLeft, const float* sideRight,
                  int numSamples) noexcept;

    float compressorGainReductionDb() const noexcept { return compressorMeter.load (std::memory_order_relaxed); }
    float gateGainReductionDb() const noexcept { return gateMeter.load (std::memory_order_relaxed); }

    /** The achievable 10 % recovery times of the compressor release, in seconds. Settings outside
        this range sit at the nearer end. */
    static void compressorReleaseRange (float& minSeconds, float& maxSeconds) noexcept;
    /** Release pot resistance (ohms) for a 10 % recovery of releaseSeconds: interpolated in log space
        from the measured table, clamped to the achievable range. */
    static float compressorPotOhms (float releaseSeconds) noexcept;
    /** Offline calibration, used to build and check the table. The 10 % recovery time of the
        compressor timing network at a pot resistance, after 2 s of the sustained 10 dB level (AUTO
        attack), measured on the same solver that process() runs. */
    static double compressorTenPercentSeconds (float potOhms, double sampleRate) noexcept;

private:
    struct Ad536
    {
        float inputPrevious = 0.0f;
        float highPass = 0.0f;
        float meanSquare = 0.0f;
    };

    struct AudioPath
    {
        float inputHighPassInput = 0.0f;
        float inputHighPass = 0.0f;
        float outputHighPassInput = 0.0f;
        float outputHighPass = 0.0f;
        float outputLowPass = 0.0f;
    };

    static float gainFromDb (float db) noexcept;
    float onePole (float seconds) const noexcept;
    /** The detector up to the mean square's root (volts rms); detectorVolts() turns the larger of
        the two channels' into the dB command voltage once per sample. */
    float detectorRms (Ad536& detector, float sample) const noexcept;
    float detectorVolts (float rms) const noexcept;
    static float diodeKneeVolts (float overVolts) noexcept;
    float dbx2151 (float inputVolts, float controlVolts) const noexcept;
    float outputStage (AudioPath& path, float inputVolts) const noexcept;
    float compressorTargetVolts (float levelVolts) const noexcept;
    float gateTargetVolts (float levelVolts) noexcept;
    float updateCompressorTiming (float targetVolts) noexcept;
    float updateGateTiming (float targetVolts) noexcept;

    /** Conductances (siemens) of the compressor timing network: the attack resistance N-X, C5 X-B
        and C6 B-0 (as backward-Euler companions, C/h), R22 B-N20, R23 N20-N and the release pot N20-0. */
    struct TimingNetwork
    {
        double attack = 0, c5 = 0, c6 = 0, r22 = 0, r23 = 0, pot = 0;
    };
    /** The timing node voltages. x is V(X), the control voltage; b is V(B). C5 holds x - b and C6 holds
        b, so these two numbers are the whole state and survive parameter changes unchanged. */
    struct TimingState
    {
        double x = 0.0, b = 0.0;
    };
    static TimingNetwork timingNetwork (double sampleRate, float attackOhms, float potOhms) noexcept;
    /** One backward-Euler step. Solves the nodal system for V(X), V(B), V(N20) and V(N); D3 drives N
        to the target when the target exceeds the floating V(N). */
    static void solveTiming (const TimingNetwork& network, TimingState& state, double targetVolts) noexcept;

    /** The one-pole coefficients and timing conductances that depend only on the rate and the
        settings: computed once per process() call instead of in every sample. */
    struct Coefficients
    {
        float detectorHighPass = 0, detectorAverage = 0, inputHighPass = 0, outputLowPass = 0, outputHighPass = 0;
        TimingNetwork timing;
    };
    void updateCoefficients() noexcept;
    Coefficients k;

    SslE10Settings settings;
    double fs = 48000.0;
    Ad536 detectors[2];
    AudioPath audioPaths[2];
    TimingState timing;
    float gateC9 = 0.0f;
    bool gateOpen = true;
    std::atomic<float> compressorMeter { 0.0f }, gateMeter { 0.0f };
};
} // namespace ar::fx
