#include "SslE10Dynamics.h"

#include "SslE242.h"

#include <algorithm>
#include <cmath>

namespace ar::fx
{
namespace
{
// T82010.71, issue 15. Values retain their drawing designators so that changes can be checked
// against the service sheet instead of being hidden in time-constant guesses.
constexpr float kR2 = 6.2e3f;          // AD536J input
constexpr float kC1 = 10.0e-6f;
constexpr float kC2 = 3.3e-9f;         // AD536J averaging capacitor
constexpr float kAd536InputOhms = 16.67e3f;
constexpr float kAd536MsPerMicrofarad = 25.0e-3f;
constexpr float kDetectorVoltsPerDb = 0.1f; // drawing: LAW = 10 dB = 1 V at TP1/TP2

constexpr float kR19 = 300.0f;         // compressor FAST attack, in parallel with the SLOW R2
constexpr float kR2Attack = 10.0e3f;   // compressor SLOW attack (drawing R2). AUTO uses it alone: the old R21 value.
constexpr float kR22 = 10.0e3f;        // corrected service value; the old public scan says 1 M
constexpr float kR23 = 10.0e3f;
constexpr float kC5 = 3.3e-6f;
constexpr float kC6 = 22.0e-6f;
// The 250 k log release pot runs from N20 to 0 V (its grid is in the table below). Chosen, not a
// drawing value: the pot's 0-ohm end is taken as a 1 ohm wiper so that the network
// stays finite. An offline check with a 0.001 ohm floor moves the 0-ohm time by 0.006 %.
constexpr float kPotWiperFloorOhms = 1.0f;

constexpr float kR30 = 36.0e3f;        // gate comparator / hysteresis network
constexpr float kR31 = 510.0e3f;
constexpr float kR34 = 36.0e3f;
constexpr float kR44 = 1.0f;           // gate FAST charge path
constexpr float kR45 = 100.0f;         // gate SLOW charge path
constexpr float kR47 = 10.0e3f;
constexpr float kC9 = 6.8e-6f;
constexpr float kGateReleasePot = 250.0e3f;
constexpr float kBc184SaturatedOhms = 5.0f;

constexpr float kR55 = 100.0e3f;       // compressor knee / ratio network
constexpr float kR56 = 5.1e3f;
constexpr float kR88 = 56.0e3f;
constexpr float kR20 = 8.2e3f;
constexpr float kDiodeIdeality = 1.8f; // 1S44 / 1N4148 class
constexpr float kThermalVolts = 25.85e-3f;

constexpr float kR79 = 20.0e3f;        // DBX 2151 input and 5534 transimpedance stage
constexpr float kC10 = 2.2e-6f;
constexpr float kR81 = 20.0e3f;
constexpr float kC12 = 100.0e-12f;
constexpr float kC13 = 220.0e-6f;
constexpr float kC14 = 220.0e-6f;
constexpr float kR84 = 10.0f;
constexpr float kR85 = 10.0e3f;
constexpr float kNe5534LinearVolts = 14.85f;
constexpr float kNe5534RailVolts = 16.5f;
constexpr float kControlVoltsPerDb = 0.25f; // drawing: 10 dB = 2V5 on the VCA command bus
constexpr float kDbxTestPeakVolts = 180.0e-6f * kR79; // data-sheet test current through R79

float clampTiny (float x) noexcept
{
    return std::abs (x) < 1.0e-20f ? 0.0f : x;
}

double clampTiny (double x) noexcept
{
    return std::abs (x) < 1.0e-20 ? 0.0 : x;
}

float parallel (float a, float b) noexcept
{
    return a * b / (a + b);
}

// The ratio control: 20 means LIMIT, a ratio of 1000. Returns the feed-forward travel 1 - 1/ratio.
float ratioTravelOf (float ratioSetting) noexcept
{
    const float ratio = ratioSetting >= 19.5f ? 1000.0f : std::max (1.0f, ratioSetting);
    return 1.0f - 1.0f / ratio;
}

// Release table, measured offline by compressorTenPercentSeconds() (classic_channel_test --release-table prints
// it at 48 kHz; the 44.1 and 96 kHz values agree to 0.00002 s). Pot grid: 0 ohms, then 32
// log-spaced points 0.1 decade apart ending at 250 k. The times are the 10 % recovery after 2 s
// of the sustained 10 dB level, in seconds.
// The 10 % time is not monotonic at the low end: C6 drains through R22 when the pot is near 0 ohms,
// so 0 ohms (0.144 s) is slower than about 790 ohms (0.1403 s, the fastest release the network
// has). Steps below kReleaseFirstStep are kept as measured but never used for the mapping.
constexpr int kReleaseSteps = 33;
constexpr int kReleaseFirstStep = 7;
constexpr float kReleaseOhms[kReleaseSteps] = {
    0.00f, 198.58f, 250.00f, 314.73f, 396.22f, 498.82f,
    627.97f, 790.57f, 995.27f, 1252.97f, 1577.39f, 1985.82f,
    2500.00f, 3147.31f, 3962.23f, 4988.16f, 6279.72f, 7905.69f,
    9952.68f, 12529.68f, 15773.93f, 19858.21f, 25000.00f, 31473.14f,
    39622.33f, 49881.56f, 62797.16f, 79056.94f, 99526.79f, 125296.81f,
    157739.34f, 198582.06f, 250000.00f
};
constexpr float kReleaseSeconds[kReleaseSteps] = {
    0.14372f, 0.14221f, 0.14190f, 0.14155f, 0.14119f, 0.14082f,
    0.14051f, 0.14031f, 0.14035f, 0.14080f, 0.14190f, 0.14400f,
    0.14760f, 0.15344f, 0.16266f, 0.17713f, 0.20035f, 0.23963f,
    0.31230f, 0.44980f, 0.65466f, 0.90267f, 1.19591f, 1.54879f,
    1.98006f, 2.51278f, 3.17536f, 4.00315f, 5.04027f, 6.34197f,
    7.97761f, 10.03429f, 12.62155f
};
}

void SslE10Dynamics::prepare (double sampleRate) noexcept
{
    fs = std::max (1.0, sampleRate);
    updateCoefficients();
    reset();
}

void SslE10Dynamics::reset() noexcept
{
    detectors[0] = detectors[1] = {};
    audioPaths[0] = audioPaths[1] = {};
    timing = {};
    gateC9 = 0.0f;
    gateOpen = true;
    compressorMeter.store (0.0f, std::memory_order_relaxed);
    gateMeter.store (0.0f, std::memory_order_relaxed);
}

float SslE10Dynamics::gainFromDb (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

float SslE10Dynamics::onePole (float seconds) const noexcept
{
    return std::exp (-1.0f / (std::max (1.0e-8f, seconds) * (float) fs));
}

float SslE10Dynamics::detectorRms (Ad536& detector, float sample) const noexcept
{
    // C1, R2 and the AD536J's measured input resistance form the card's sidechain high-pass.
    const float inputVolts = sample * kSslVoltsPerFullScale;
    detector.highPass = k.detectorHighPass * (detector.highPass + inputVolts - detector.inputPrevious);
    detector.inputPrevious = inputVolts;

    // AD536J macromodel. Its data sheet gives 25 ms/uF for CAV. C2 is 3n3 on the drawing.
    const float square = detector.highPass * detector.highPass;
    detector.meanSquare = clampTiny (k.detectorAverage * detector.meanSquare + (1.0f - k.detectorAverage) * square);
    return std::sqrt (std::max (detector.meanSquare, 1.0e-16f));
}

float SslE10Dynamics::detectorVolts (float rms) const noexcept
{
    // VR1/R4/R6 and T2 set 0 dB to 0.7746 Vrms; R7-R12 scale the dB pin to 100 mV/dB.
    return kDetectorVoltsPerDb * (20.0f * std::log10 (rms / 0.7746f));
}

void SslE10Dynamics::updateCoefficients() noexcept
{
    const float inputTau = (kR2 + kAd536InputOhms) * kC1;
    k.detectorHighPass = inputTau / (inputTau + 1.0f / (float) fs);
    k.detectorAverage = onePole (kAd536MsPerMicrofarad * (kC2 * 1.0e6f));
    const float tauIn = kR79 * kC10;
    k.inputHighPass = tauIn / (tauIn + 1.0f / (float) fs);
    k.outputLowPass = onePole (kR81 * kC12);
    const float effectiveC = 1.0f / (1.0f / kC13 + 1.0f / kC14);
    const float tau = kR85 * effectiveC;
    k.outputHighPass = tau / (tau + 1.0f / (float) fs);

    const float attackR = settings.compressorFastAttack ? parallel (kR2Attack, kR19) : kR2Attack;
    k.timing = timingNetwork (fs, attackR, compressorPotOhms (settings.compressorReleaseSeconds));
}

void SslE10Dynamics::compressorReleaseRange (float& minSeconds, float& maxSeconds) noexcept
{
    minSeconds = kReleaseSeconds[kReleaseFirstStep];
    maxSeconds = kReleaseSeconds[kReleaseSteps - 1];
}

float SslE10Dynamics::compressorPotOhms (float seconds) noexcept
{
    constexpr int last = kReleaseSteps - 1;
    if (seconds <= kReleaseSeconds[kReleaseFirstStep])
        return kReleaseOhms[kReleaseFirstStep];
    if (seconds >= kReleaseSeconds[last])
        return kReleaseOhms[last];
    for (int i = kReleaseFirstStep; i < last; ++i)
    {
        if (seconds < kReleaseSeconds[i + 1])
        {
            // Log-space interpolation: ln(ohms) is linear in ln(seconds) between the grid points.
            const float lo = std::log (std::max (kReleaseOhms[i], kPotWiperFloorOhms));
            const float hi = std::log (std::max (kReleaseOhms[i + 1], kPotWiperFloorOhms));
            const float u = (std::log (seconds) - std::log (kReleaseSeconds[i]))
                            / (std::log (kReleaseSeconds[i + 1]) - std::log (kReleaseSeconds[i]));
            return std::exp (lo + u * (hi - lo));
        }
    }
    return kReleaseOhms[last];
}

SslE10Dynamics::TimingNetwork SslE10Dynamics::timingNetwork (double sampleRate, float attackOhms, float potOhms) noexcept
{
    TimingNetwork n;
    n.attack = 1.0 / (double) attackOhms;
    n.c5 = (double) kC5 * sampleRate;   // backward Euler: C / h
    n.c6 = (double) kC6 * sampleRate;
    n.r22 = 1.0 / (double) kR22;
    n.r23 = 1.0 / (double) kR23;
    n.pot = 1.0 / (double) std::max (potOhms, kPotWiperFloorOhms);
    return n;
}

namespace
{
/** Solves the 4 x 4 system m x = rhs (the rhs is column 4), in place, by Gaussian elimination with
    partial pivoting. Doubles: the conductances span about six decades. Fixed size, no allocation. */
void solveFourByFour (double (&m)[4][5]) noexcept
{
    for (int col = 0; col < 4; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::abs (m[r][col]) > std::abs (m[pivot][col]))
                pivot = r;
        if (pivot != col)
            for (int c = 0; c < 5; ++c)
                std::swap (m[col][c], m[pivot][c]);
        for (int r = col + 1; r < 4; ++r)
        {
            const double f = m[r][col] / m[col][col];
            for (int c = col; c < 5; ++c)
                m[r][c] -= f * m[col][c];
        }
    }
    for (int r = 3; r >= 0; --r)
    {
        double sum = m[r][4];
        for (int c = r + 1; c < 4; ++c)
            sum -= m[r][c] * m[c][4];
        m[r][4] = sum / m[r][r];
    }
}
}

void SslE10Dynamics::solveTiming (const TimingNetwork& n, TimingState& state, double targetVolts) noexcept
{
    // Node order X, B, N20, N. C5 (X-B) and C6 (B-0) are backward-Euler companions around the
    // previous state; the attack resistance joins N and X; R22 joins B and N20; R23 joins N20 and N.
    const double c5Charge = state.x - state.b;   // C5's voltage at the start of the step
    double m[4][5] = {
        { n.c5 + n.attack, -n.c5, 0.0, -n.attack, n.c5 * c5Charge },
        { -n.c5, n.c5 + n.c6 + n.r22, -n.r22, 0.0, -n.c5 * c5Charge + n.c6 * state.b },
        { 0.0, -n.r22, n.r22 + n.r23 + n.pot, -n.r23, 0.0 },
        { -n.attack, 0.0, -n.r23, n.attack + n.r23, 0.0 },   // N floating: only R23 and the attack R meet
    };

    double driven[4][5];
    std::copy (&m[0][0], &m[0][0] + 20, &driven[0][0]);
    solveFourByFour (m);
    if (m[3][4] < targetVolts)
    {
        // D3 conducts: N is driven to the target. Only its row changes.
        driven[3][0] = 0.0;
        driven[3][1] = 0.0;
        driven[3][2] = 0.0;
        driven[3][3] = 1.0;
        driven[3][4] = targetVolts;
        solveFourByFour (driven);
        state.x = clampTiny (driven[0][4]);
        state.b = clampTiny (driven[1][4]);
    }
    else
    {
        state.x = clampTiny (m[0][4]);
        state.b = clampTiny (m[1][4]);
    }
}

double SslE10Dynamics::compressorTenPercentSeconds (float potOhms, double sampleRate) noexcept
{
    const TimingNetwork n = timingNetwork (sampleRate, kR2Attack, potOhms);
    constexpr double sustainedVolts = 10.0 * kControlVoltsPerDb;   // 10 dB of reduction
    TimingState s;
    const int hold = (int) (2.0 * sampleRate);
    for (int i = 0; i < hold; ++i)
        solveTiming (n, s, sustainedVolts);
    const double start = s.x;
    const int limit = (int) (60.0 * sampleRate);   // the 250 k end recovers in 12.6 s
    for (int i = 0; i < limit; ++i)
    {
        const double before = s.x;
        solveTiming (n, s, 0.0);
        if (s.x <= 0.1 * start)
        {
            const double fraction = (before - 0.1 * start) / std::max (1.0e-300, before - s.x);
            return (i + fraction) / sampleRate;
        }
    }
    return 60.0;
}

float SslE10Dynamics::diodeKneeVolts (float overVolts) noexcept
{
    // D14/D1 and R88/R56/R55/R20 make the issue-12-and-later "over easy" knee. A Shockley
    // soft rectifier is used here, with the thermal slope expanded by the actual resistor
    // loading. Unlike the old quadratic knee, the transition is the diode network's curve.
    const float loading = 1.0f + parallel (kR88, kR55) / (kR56 + kR20);
    const float slope = kDiodeIdeality * kThermalVolts * loading;
    const float x = overVolts / slope;
    if (x > 20.0f)
        return overVolts;
    if (x < -20.0f)
        return slope * std::exp (x);
    return slope * std::log1p (std::exp (x));
}

float SslE10Dynamics::compressorTargetVolts (float levelVolts) const noexcept
{
    const float thresholdVolts = settings.compressorThresholdDb * kDetectorVoltsPerDb;
    // The two 10 k linear ratio gangs form the feed-forward slope.
    const float ratioTravel = ratioTravelOf (settings.compressorRatio);
    const float conducted = diodeKneeVolts (levelVolts - thresholdVolts);
    const float reductionDb = conducted / kDetectorVoltsPerDb * ratioTravel;
    return std::max (0.0f, reductionDb * kControlVoltsPerDb);
}

float SslE10Dynamics::gateTargetVolts (float levelVolts) noexcept
{
    const float thresholdVolts = settings.gateThresholdDb * kDetectorVoltsPerDb;
    // D8/D9, equal R30/R34 and R31 give a few dB of positive-feedback hysteresis, increasing
    // toward the bottom of the threshold pot. D1 (1V5 zener) fixes the diode operating point.
    const float diodeDrop = 0.55f;
    const float fixedHysteresis = diodeDrop * kR30 / (kR30 + kR34);
    const float thresholdFeedback = std::max (0.0f, -thresholdVolts) * kR30 / kR31;
    const float hysteresisVolts = fixedHysteresis + thresholdFeedback;

    if (gateOpen && levelVolts < thresholdVolts - hysteresisVolts)
        gateOpen = false;
    else if (! gateOpen && levelVolts >= thresholdVolts)
        gateOpen = true;

    float reductionDb = 0.0f;
    if (settings.expander)
    {
        // Equal 36 k arms R29/R30 give the 2:1 downward-expander slope.
        reductionDb = std::min (settings.gateRangeDb,
                                std::max (0.0f, (thresholdVolts - levelVolts)
                                                        / kDetectorVoltsPerDb * 0.5f));
    }
    else if (! gateOpen)
    {
        reductionDb = settings.gateRangeDb;
    }
    return reductionDb * kControlVoltsPerDb;
}

float SslE10Dynamics::updateCompressorTiming (float targetVolts) noexcept
{
    solveTiming (k.timing, timing, (double) targetVolts);
    return (float) timing.x;   // CV = V(X)
}

float SslE10Dynamics::updateGateTiming (float targetVolts) noexcept
{
    if (targetVolts < gateC9)
    {
        // TR4 selects R44 (FAST) or R45 (SLOW); the BC184's saturated resistance is included.
        const float chargeR = (settings.gateFastAttack ? kR44 : kR45) + kBc184SaturatedOhms;
        const float a = onePole (chargeR * kC9);
        gateC9 = a * gateC9 + (1.0f - a) * targetVolts;
    }
    else
    {
        const float wantedTau = std::max (0.001f, settings.gateReleaseSeconds / 2.3025851f);
        const float releaseR = std::clamp (wantedTau / kC9 - kR47, 0.0f, kGateReleasePot);
        const float a = onePole ((kR47 + releaseR) * kC9);
        gateC9 = a * gateC9 + (1.0f - a) * targetVolts;
    }
    gateC9 = clampTiny (gateC9);
    return gateC9;
}

float SslE10Dynamics::dbx2151 (float inputVolts, float controlVolts) const noexcept
{
    // R75/R76 translate the card's 250 mV/dB command bus into the 2151's control current.
    // The exponential law is the VCA's defining transfer, not an ideal post-process multiplier.
    const float gainDb = -4.0f * std::clamp (controlVolts, -10.0f, 25.0f);
    const float gain = gainFromDb (gainDb);

    // The 2151 data sheet specifies 0.004% typical THD at 0 dB and 0.025% at +/-15 dB,
    // measured at 180 uA. R79 is 20 k, so that test current is 3.6 V peak on this card.
    const float commandDb = std::min (15.0f, std::abs (gainDb));
    const float thd3 = 0.00004f + commandDb / 15.0f * (0.00025f - 0.00004f);
    const float x = std::clamp (inputVolts / kDbxTestPeakVolts, -3.0f, 3.0f);
    const float blackmer = x + 4.0f * thd3 * x * x * x + 2.0e-5f * x * x;
    return gain * kDbxTestPeakVolts * blackmer;
}

float SslE10Dynamics::outputStage (AudioPath& path, float inputVolts) const noexcept
{
    // T9 NE5534: R81 || C12. The DBX current-output polarity makes the complete stage
    // non-inverting. Its finite rails are the same +/-18 V 5534 model used by the EQ card.
    const float a = k.outputLowPass;
    path.outputLowPass = a * path.outputLowPass + (1.0f - a) * inputVolts;
    float v = path.outputLowPass;
    const float magnitude = std::abs (v);
    if (magnitude > kNe5534LinearVolts)
    {
        const float knee = kNe5534RailVolts - kNe5534LinearVolts;
        v = std::copysign (kNe5534LinearVolts
                              + knee * std::tanh ((magnitude - kNe5534LinearVolts) / knee), v);
    }

    // C13/C14 are back-to-back 220 uF (110 uF effective), R85 is the 10 k output load.
    path.outputHighPass = k.outputHighPass * (path.outputHighPass + v - path.outputHighPassInput);
    path.outputHighPassInput = v;
    return path.outputHighPass * (kR85 / (kR84 + kR85));
}

void SslE10Dynamics::process (float* left, float* right, const float* sideLeft,
                              const float* sideRight, int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr)
        return;
    if (! settings.enabled)
    {
        // The console switch removes the card from the audio path. Preserve exact digital bypass.
        reset();
        return;
    }

    updateCoefficients();
    const float ratioTravel = ratioTravelOf (settings.compressorRatio);
    // The other gang of the 10 k dual threshold pot supplies the card's automatic make-up.
    const float makeupDb = settings.automaticMakeup
                               ? std::max (0.0f, 0.5f * (10.0f - settings.compressorThresholdDb)
                                                       * ratioTravel)
                               : 0.0f;
    const float makeupVolts = makeupDb * kControlVoltsPerDb;

    const auto inputCoupling = [this] (AudioPath& path, float sample)
    {
        // C10 sees R79's virtual-ground input. R78 is on the driven side of C10, so the
        // motherboard's ideal low-impedance drive makes it a load, not part of this pole.
        path.inputHighPass = k.inputHighPass * (path.inputHighPass + sample - path.inputHighPassInput);
        path.inputHighPassInput = sample;
        return path.inputHighPass;
    };

    float highestComp = 0.0f, highestGate = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float sl = sideLeft != nullptr ? sideLeft[i] : left[i];
        const float sr = sideRight != nullptr ? sideRight[i]
                                               : (right != nullptr ? right[i] : sl);
        const float rmsLeft = detectorRms (detectors[0], sl);
        const float rmsRight = detectorRms (detectors[1], sr);
        // SW2 and D27 link the command buses; the card asking for more attenuation wins (the dB
        // law is monotonic, so the larger rms is the larger level: one log per sample, not two).
        const float levelVolts = detectorVolts (std::max (rmsLeft, rmsRight));

        const float compVolts = updateCompressorTiming (compressorTargetVolts (levelVolts));
        // The plug-in never opens the gate's range (0 dB), so its comparator and timing network sit at
        // zero: skipped while they are (settings.gateRangeDb > 0 still runs them, as the tests do).
        const bool gateIdle = settings.gateRangeDb == 0.0f && gateC9 == 0.0f;
        const float gateVolts = gateIdle ? 0.0f : updateGateTiming (gateTargetVolts (levelVolts));
        const float commandVolts = compVolts + gateVolts - makeupVolts;

        float l = dbx2151 (inputCoupling (audioPaths[0], left[i] * kSslVoltsPerFullScale), commandVolts);
        l = outputStage (audioPaths[0], l) / kSslVoltsPerFullScale;
        left[i] = l;
        if (right != nullptr)
        {
            float r = dbx2151 (inputCoupling (audioPaths[1], right[i] * kSslVoltsPerFullScale), commandVolts);
            r = outputStage (audioPaths[1], r) / kSslVoltsPerFullScale;
            right[i] = r;
        }

        highestComp = std::max (highestComp, compVolts / kControlVoltsPerDb);
        highestGate = std::max (highestGate, gateVolts / kControlVoltsPerDb);
    }
    compressorMeter.store (highestComp, std::memory_order_relaxed);
    gateMeter.store (highestGate, std::memory_order_relaxed);
}
} // namespace ar::fx
