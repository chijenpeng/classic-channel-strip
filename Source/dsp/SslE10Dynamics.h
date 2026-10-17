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
    float compressorReleaseSeconds = 0.4f;
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
    float detectorVolts (Ad536& detector, float sample) const noexcept;
    static float diodeKneeVolts (float overVolts) noexcept;
    float dbx2151 (float inputVolts, float controlVolts) const noexcept;
    float outputStage (AudioPath& path, float inputVolts) const noexcept;
    float compressorTargetVolts (float levelVolts) const noexcept;
    float gateTargetVolts (float levelVolts) noexcept;
    float updateCompressorTiming (float targetVolts) noexcept;
    float updateGateTiming (float targetVolts) noexcept;

    SslE10Settings settings;
    double fs = 48000.0;
    Ad536 detectors[2];
    AudioPath audioPaths[2];
    float compressorC5 = 0.0f, compressorC6 = 0.0f, gateC9 = 0.0f;
    bool gateOpen = true;
    std::atomic<float> compressorMeter { 0.0f }, gateMeter { 0.0f };
};
} // namespace ar::fx
