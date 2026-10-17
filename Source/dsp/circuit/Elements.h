#pragma once

#include "LinearNetwork.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace ar::circuit
{
#ifndef AR_SINGLE_CORRECTION
#define AR_SINGLE_CORRECTION 0
#endif
constexpr bool kSingleCorrection = AR_SINGLE_CORRECTION != 0;
#ifndef AR_SINGLE_CORRECTION_MAX
#define AR_SINGLE_CORRECTION_MAX 0.05
#endif
constexpr double kSingleCorrectionMax = AR_SINGLE_CORRECTION_MAX;

struct PortRef
{
    LinearNetwork* net = nullptr;
    int port = -1;
    int netIndex = -1;   // position of the network in the circuit, for ordering
};

/** One nonlinear device described by its port currents, model voltages and local Jacobian. */
class Device
{
public:
    virtual ~Device() = default;
    virtual int numPorts() const noexcept = 0;
    virtual int numUnknowns() const noexcept = 0;
    virtual int numModelVoltages() const noexcept = 0;
    /** Fills C (ports x m), P (nV x ports) and Rint (nV x m), all row-major and zeroed. */
    virtual void coefficients (Real* C, Real* P, Real* Rint) const noexcept = 0;
    /** Model currents u and the Jacobian dU/dV (m x nV, row-major, zeroed on entry). */
    virtual void evaluate (const Real* vModel, Real* uModel, Real* dUdV) const noexcept = 0;
    virtual Real limitStep (int i, Real vOld, Real delta) const noexcept = 0;
    virtual bool hasAffineRegion() const noexcept { return false; }
    virtual bool affineBetween (int, Real, Real) const noexcept { return false; }

    static Real limitFlat (Real delta, Real lim) noexcept { return delta > lim ? lim : (delta < -lim ? -lim : delta); }

    std::vector<PortRef> ports;
};

/** A set of devices solved together with Newton's method against the networks' affine port
    relations. Strongly coupled devices in one network belong in the same group. */
class DeviceGroup
{
public:
    explicit DeviceGroup (std::vector<std::unique_ptr<Device>> devices);

    void solve (bool resweep = false) noexcept;
    bool singleNetwork() const noexcept { for (const auto& p : ports) if (p.net != ports[0].net) return false; return true; }
    bool injects (int p) const noexcept { for (int j = 0; j < m; ++j) if (C[(size_t) p * (size_t) m + (size_t) j] != 0.0) return true; return false; }
    std::vector<std::unique_ptr<Device>> takeDevices() noexcept { return std::move (devices); }
    int maxNetIndex() const noexcept;
    void resetState() noexcept
    {
        for (auto* v : { &u, &uPrev, &uPrev2, &vSolved })
            std::fill (v->begin(), v->end(), 0.0);
    }
    void rebuildCoefficients() noexcept;

    const std::vector<PortRef>& getPorts() const noexcept { return ports; }
    const std::vector<Real>& unknowns() const noexcept { return u; }

    int maxIterations = 60;
    Real toleranceVolts = 1.0e-4;

    static inline unsigned long statIterations = 0, statSolves = 0, statFailures = 0;
    unsigned long failures = 0;
    unsigned long newtonIterations = 0, newtonSolves = 0;

private:
    void refreshCoupling() noexcept;
    void computeOpenVoltages() noexcept;
    bool iterate (Real limitScale, int iterations) noexcept;
    bool iterateLimited (int iterations) noexcept;
    Real acceptLimit (bool firstStep) const noexcept
    {
        return kSingleCorrection && firstStep ? std::max (50.0 * toleranceVolts, kSingleCorrectionMax) : 50.0 * toleranceVolts;
    }

    unsigned cachedRefactorSum = ~0u;
    bool affineDevices = false;
    bool jacobianFactored = false;
    std::vector<Real> jacobianKey, jacobianLu;
    std::vector<int> jacobianPivot;
    std::vector<std::unique_ptr<Device>> devices;
    std::vector<PortRef> ports;
    int m = 0, nV = 0;
    std::vector<Real> C, P, Rint, u, uPrev, uPrev2, uLast;
    std::vector<Real> v0, reff, M, vm0, vm, um, dUdV, F, J, du;
    std::vector<Real> vEval, vSolved;
    std::vector<std::pair<Device*, int>> voltageOwner;
    std::vector<int> devUnknowns, devVoltages;
    std::vector<int> portNode, otherStart, otherPort;
    std::vector<Real> otherR;
};

/** A soft-rail op-amp: inputs draw no current and the output is a voltage source behind `outputOhms`.
    With `linearFraction` > 0 it stays exactly linear through most of the swing. */
class OpAmpDevice final : public Device
{
public:
    OpAmpDevice (Real vMin, Real vMax, PortRef inPlus, PortRef inMinus, PortRef out, Real openLoopGain = 1500.0,
                 Real outputOhms = 50.0, Real linearFraction = 0.0);
    int numPorts() const noexcept override { return 3; }
    int numUnknowns() const noexcept override { return 1; }
    int numModelVoltages() const noexcept override { return 2; }
    void coefficients (Real* C, Real* P, Real* Rint) const noexcept override;
    void evaluate (const Real* v, Real* u, Real* j) const noexcept override;
    Real limitStep (int i, Real vOld, Real delta) const noexcept override
    {
        if (i == 1) return limitFlat (delta, 20.0);
        const Real edge = 0.5 * (vMax - vMin);
        const Real vNew = vOld + delta;
        if (knee > 0.0)
        {
            if (std::abs (vNew) <= knee)
                return delta;
            if ((vOld > 0.0) != (vNew > 0.0) || std::abs (vOld) < knee * (1.0 - 1.0e-9))
                return (vNew > 0.0 ? knee : -knee) - vOld;
            return delta;
        }
        if ((vOld > 0.0) != (vNew > 0.0))
            return (vNew < -edge ? -edge : (vNew > edge ? edge : vNew)) - vOld;
        return delta;
    }
    bool hasAffineRegion() const noexcept override { return knee > 0.0; }
    bool affineBetween (int i, Real a, Real b) const noexcept override
    {
        return i == 1 || (knee > 0.0 && std::abs (a) <= knee && std::abs (b) <= knee);
    }

private:
    Real vMin, vMax, gain, ro;
    Real knee = 0.0;
};
} // namespace ar::circuit
