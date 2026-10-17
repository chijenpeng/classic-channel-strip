#include "Elements.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ar::circuit
{
namespace
{
    /** Solves the small dense system a·x = b in place (a is n x n row-major, b becomes x). */
    void solveSmall (Real* a, Real* b, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
        {
            int best = k;
            for (int i = k + 1; i < n; ++i)
                if (std::abs (a[i * n + k]) > std::abs (a[best * n + k]))
                    best = i;
            if (best != k)
            {
                for (int j = 0; j < n; ++j) std::swap (a[k * n + j], a[best * n + j]);
                std::swap (b[k], b[best]);
            }
            Real d = a[k * n + k];
            if (std::abs (d) < 1.0e-30) d = 1.0e-30;
            for (int i = k + 1; i < n; ++i)
            {
                const Real f = a[i * n + k] / d;
                if (f == 0.0) continue;
                for (int j = k; j < n; ++j) a[i * n + j] -= f * a[k * n + j];
                b[i] -= f * b[k];
            }
        }
        for (int i = n - 1; i >= 0; --i)
        {
            Real s = b[i];
            for (int j = i + 1; j < n; ++j) s -= a[i * n + j] * b[j];
            b[i] = s / a[i * n + i];
        }
    }

    /** LU factorisation with partial pivoting in place (unit lower, upper with reciprocal
        diagonal), for solving the same small system again (luSolve). */
    void luFactor (Real* a, int* piv, int n) noexcept
    {
        for (int k = 0; k < n; ++k)
        {
            int best = k;
            for (int i = k + 1; i < n; ++i)
                if (std::abs (a[i * n + k]) > std::abs (a[best * n + k]))
                    best = i;
            piv[k] = best;
            if (best != k)
                for (int j = 0; j < n; ++j) std::swap (a[k * n + j], a[best * n + j]);
            Real d = a[k * n + k];
            if (std::abs (d) < 1.0e-30) d = 1.0e-30;
            const Real inv = 1.0 / d;
            for (int i = k + 1; i < n; ++i)
            {
                const Real f = a[i * n + k] * inv;
                a[i * n + k] = f;
                if (f != 0.0)
                    for (int j = k + 1; j < n; ++j) a[i * n + j] -= f * a[k * n + j];
            }
            a[k * n + k] = inv;
        }
    }

    void luSolve (const Real* a, const int* piv, Real* b, int n) noexcept
    {
        // Whole rows were swapped (multipliers too), so permute b completely first.
        for (int k = 0; k < n; ++k)
            if (piv[k] != k) std::swap (b[k], b[piv[k]]);
        for (int k = 0; k < n; ++k)
            for (int i = k + 1; i < n; ++i) b[i] -= a[i * n + k] * b[k];
        for (int i = n - 1; i >= 0; --i)
        {
            Real s = b[i];
            for (int j = i + 1; j < n; ++j) s -= a[i * n + j] * b[j];
            b[i] = s * a[i * n + i];
        }
    }


}

//==============================================================================
DeviceGroup::DeviceGroup (std::vector<std::unique_ptr<Device>> d) : devices (std::move (d))
{
    int np = 0;
    for (const auto& dev : devices)
    {
        np += dev->numPorts();
        m += dev->numUnknowns();
        nV += dev->numModelVoltages();
        for (const auto& p : dev->ports)
            ports.push_back (p);
    }
    C.assign ((size_t) np * (size_t) m, 0.0);
    P.assign ((size_t) nV * (size_t) np, 0.0);
    Rint.assign ((size_t) nV * (size_t) m, 0.0);
    u.assign ((size_t) m, 0.0);
    uPrev = u;
    uPrev2 = u;
    uLast = u;
    for (const auto& dev : devices)
    {
        devUnknowns.push_back (dev->numUnknowns());
        devVoltages.push_back (dev->numModelVoltages());
    }
    v0.assign ((size_t) np, 0.0);
    reff.assign ((size_t) np * (size_t) m, 0.0);
    M.assign ((size_t) nV * (size_t) m, 0.0);
    vm0.assign ((size_t) nV, 0.0);
    vm.assign ((size_t) nV, 0.0);
    vEval.assign ((size_t) nV, 0.0);
    vSolved.assign ((size_t) nV, 0.0);
    um.assign ((size_t) m, 0.0);
    dUdV.assign ((size_t) m * (size_t) nV, 0.0);
    F.assign ((size_t) m, 0.0);
    J.assign ((size_t) m * (size_t) m, 0.0);
    du.assign ((size_t) m, 0.0);
    for (const auto& dev : devices)
        for (int i = 0; i < dev->numModelVoltages(); ++i)
            voltageOwner.push_back ({ dev.get(), i });
    affineDevices = ! devices.empty() && std::all_of (devices.begin(), devices.end(), [] (const auto& dev) { return dev->hasAffineRegion(); });
    jacobianKey.assign ((size_t) m * (size_t) m, 0.0);
    jacobianLu = jacobianKey;
    jacobianPivot.assign ((size_t) m, 0);
    rebuildCoefficients();
}

void DeviceGroup::rebuildCoefficients() noexcept
{
    cachedRefactorSum = ~0u;
    std::fill (uPrev.begin(), uPrev.end(), 0.0);
    std::fill (uPrev2.begin(), uPrev2.end(), 0.0);
    std::fill (u.begin(), u.end(), 0.0);
    std::fill (C.begin(), C.end(), 0.0);
    std::fill (P.begin(), P.end(), 0.0);
    std::fill (Rint.begin(), Rint.end(), 0.0);
    const int np = (int) ports.size();
    int p0 = 0, m0 = 0, v0i = 0;
    std::vector<Real> c, p, r;
    for (const auto& dev : devices)
    {
        const int dp = dev->numPorts(), dm = dev->numUnknowns(), dv = dev->numModelVoltages();
        c.assign ((size_t) dp * (size_t) dm, 0.0);
        p.assign ((size_t) dv * (size_t) dp, 0.0);
        r.assign ((size_t) dv * (size_t) dm, 0.0);
        dev->coefficients (c.data(), p.data(), r.data());
        for (int i = 0; i < dp; ++i)
            for (int j = 0; j < dm; ++j)
                C[(size_t) (p0 + i) * (size_t) m + (size_t) (m0 + j)] = c[(size_t) i * (size_t) dm + (size_t) j];
        for (int i = 0; i < dv; ++i)
        {
            for (int j = 0; j < dp; ++j)
                P[(size_t) (v0i + i) * (size_t) np + (size_t) (p0 + j)] = p[(size_t) i * (size_t) dp + (size_t) j];
            for (int j = 0; j < dm; ++j)
                Rint[(size_t) (v0i + i) * (size_t) m + (size_t) (m0 + j)] = r[(size_t) i * (size_t) dm + (size_t) j];
        }
        p0 += dp; m0 += dm; v0i += dv;
    }
}

int DeviceGroup::maxNetIndex() const noexcept
{
    int best = -1;
    for (const auto& p : ports)
        best = std::max (best, p.netIndex);
    return best;
}

void DeviceGroup::refreshCoupling() noexcept
{
    const int np = (int) ports.size();
    portNode.assign ((size_t) np, 0);
    otherStart.assign ((size_t) np + 1, 0);
    otherPort.clear();
    otherR.clear();
    auto isOwnPort = [this] (const LinearNetwork* net, int q)
    {
        for (const auto& own : ports)
            if (own.net == net && own.port == q)
                return true;
        return false;
    };
    for (int p = 0; p < np; ++p)
    {
        auto* net = ports[(size_t) p].net;
        const int pp = ports[(size_t) p].port;
        portNode[(size_t) p] = net->portNode (pp);
        for (int q = 0; q < net->numPorts(); ++q)
        {
            const Real r = net->portResistance (pp, q);
            if (r != 0.0 && ! isOwnPort (net, q))
            {
                otherPort.push_back (q);
                otherR.push_back (r);
            }
        }
        otherStart[(size_t) p + 1] = (int) otherPort.size();
    }
    for (int p = 0; p < np; ++p)
    {
        auto* net = ports[(size_t) p].net;
        for (int j = 0; j < m; ++j)
        {
            Real r = 0.0;
            for (int q = 0; q < np; ++q)
                if (ports[(size_t) q].net == net)
                {
                    const Real c = C[(size_t) q * (size_t) m + (size_t) j];
                    if (c != 0.0)
                        r += net->portResistance (ports[(size_t) p].port, ports[(size_t) q].port) * c;
                }
            reff[(size_t) p * (size_t) m + (size_t) j] = r;
        }
    }
    for (int i = 0; i < nV; ++i)
        for (int j = 0; j < m; ++j)
        {
            Real t = Rint[(size_t) i * (size_t) m + (size_t) j];
            for (int p = 0; p < np; ++p)
                t += P[(size_t) i * (size_t) np + (size_t) p] * reff[(size_t) p * (size_t) m + (size_t) j];
            M[(size_t) i * (size_t) m + (size_t) j] = t;
        }
}

#if AR_PROFILE_OPS
unsigned long long profModel = 0, profModelCalls = 0, profPrologue = 0, profNewton = 0, profEpilogue = 0;
#endif
bool DeviceGroup::iterate (Real limitScale, int iterations) noexcept
{
    // Newton with per-voltage step limiting (as SPICE does for junctions): the full step is
    // scaled so no model voltage moves further than its device allows.
    auto evaluateAt = [this] (Real& residualNorm)
    {
        for (int i = 0; i < nV; ++i)
        {
            Real s = vm0[(size_t) i];
            for (int j = 0; j < m; ++j)
                s += M[(size_t) i * (size_t) m + (size_t) j] * u[(size_t) j];
            vm[(size_t) i] = s;
        }
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < m; ++k)
                J[(size_t) j * (size_t) m + (size_t) k] = (j == k) ? 1.0 : 0.0;
        int m0 = 0, v0i = 0;
        for (size_t d = 0; d < devices.size(); ++d)
        {
            const int dm = devUnknowns[d], dv = devVoltages[d];
            Real* localJ = dUdV.data();
            std::fill (localJ, localJ + (size_t) dm * (size_t) dv, 0.0);
#if AR_PROFILE_OPS
            { const auto t0 = __builtin_readcyclecounter(); devices[d]->evaluate (vm.data() + v0i, um.data() + m0, localJ); profModel += __builtin_readcyclecounter() - t0; ++profModelCalls; }
#else
            devices[d]->evaluate (vm.data() + v0i, um.data() + m0, localJ);
#endif
            for (int i = 0; i < dm; ++i)
            {
                Real* jrow = &J[(size_t) (m0 + i) * (size_t) m];
                for (int l = 0; l < dv; ++l)
                {
                    const Real g = localJ[(size_t) i * (size_t) dv + (size_t) l];
                    if (g == 0.0) continue;
                    const Real* mrow = &M[(size_t) (v0i + l) * (size_t) m];
                    for (int k = 0; k < m; ++k)
                        jrow[k] -= g * mrow[k];
                }
            }
            m0 += dm; v0i += dv;
        }
        residualNorm = 0.0;
        for (int j = 0; j < m; ++j)
        {
            F[(size_t) j] = um[(size_t) j] - u[(size_t) j];
            residualNorm += F[(size_t) j] * F[(size_t) j];
        }
    };

    Real norm = 0.0;
    evaluateAt (norm);
    if (! std::isfinite (norm))
        return false;

    for (int iter = 0; iter < iterations; ++iter)
    {
        ++statIterations; ++newtonIterations;
        std::copy (F.begin(), F.end(), du.begin());
        solveSmall (J.data(), du.data(), m);

        Real lambda = 1.0, biggest = 0.0;
        for (int i = 0; i < nV; ++i)
        {
            Real dv = 0.0;
            for (int j = 0; j < m; ++j)
                dv += M[(size_t) i * (size_t) m + (size_t) j] * du[(size_t) j];
            // Convergence is judged relative to the size of the voltage: a plate at 400 V
            // does not need the same absolute precision as a grid at -1 V.
            biggest = std::max (biggest, std::abs (dv) / std::max (1.0, std::abs (vm[(size_t) i]) * 0.05));
            if (dv == 0.0) continue;
            const auto& owner = voltageOwner[(size_t) i];
            const Real allowed = owner.first->limitStep (owner.second, vm[(size_t) i], dv * limitScale);
            const Real ratio = std::abs (allowed) / std::abs (dv);
            if (ratio < lambda)
                lambda = std::max (ratio, 1.0e-6);
        }
        if (! std::isfinite (biggest))
            return false;
        if (biggest < toleranceVolts)
        {
            // Converged; the step just computed is free and leaves only a quadratic error.
            // (Returning without it left up to the tolerance at every plate, which the stages
            // downstream amplify into audible hash.)
            for (int j = 0; j < m; ++j)
                u[(size_t) j] += du[(size_t) j];
            return true;
        }

        for (int j = 0; j < m; ++j)
            u[(size_t) j] += lambda * du[(size_t) j];
        if (lambda == 1.0 && biggest < acceptLimit (iter == 0 && limitScale == 1.0))
            return true;   // a full, tiny step: converged to working precision, no re-evaluation needed
        evaluateAt (norm);
        if (! std::isfinite (norm))
            return false;
    }
    return false;
}

bool DeviceGroup::iterateLimited (int iterations) noexcept
{
    // SPICE's scheme: every model voltage is limited on its own (pnjlim), the devices are
    // linearised at those limited voltages, and Newton takes the full step of the linearised
    // system. In iterate() one junction far from its solution scales down every variable's
    // step, which makes a large group (four merged fuzz stages) crawl through 10-30 steps.
    // The limits are taken from the last solution, which also tames the predictor's
    // overshoot on a junction that switches.
    std::copy (vSolved.begin(), vSolved.end(), vEval.begin());
    bool limited = false;
    auto evaluateAt = [this, &limited] (Real& residualNorm)
    {
        limited = false;
        for (int i = 0; i < nV; ++i)
        {
            Real s = vm0[(size_t) i];
            for (int j = 0; j < m; ++j)
                s += M[(size_t) i * (size_t) m + (size_t) j] * u[(size_t) j];
            vm[(size_t) i] = s;
        }
        for (int i = 0; i < nV; ++i)
        {
            const Real old = vEval[(size_t) i], want = vm[(size_t) i] - old;
            const auto& owner = voltageOwner[(size_t) i];
            const Real allowed = want == 0.0 ? 0.0 : owner.first->limitStep (owner.second, old, want);
            limited = limited || allowed != want;
            vEval[(size_t) i] = old + allowed;
        }
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < m; ++k)
                J[(size_t) j * (size_t) m + (size_t) k] = (j == k) ? 1.0 : 0.0;
        int m0 = 0, v0i = 0;
        for (size_t d = 0; d < devices.size(); ++d)
        {
            const int dm = devUnknowns[d], dv = devVoltages[d];
            Real* localJ = dUdV.data();
            std::fill (localJ, localJ + (size_t) dm * (size_t) dv, 0.0);
            devices[d]->evaluate (vEval.data() + v0i, um.data() + m0, localJ);
            for (int i = 0; i < dm; ++i)
            {
                Real* jrow = &J[(size_t) (m0 + i) * (size_t) m];
                for (int l = 0; l < dv; ++l)
                {
                    const Real g = localJ[(size_t) i * (size_t) dv + (size_t) l];
                    if (g == 0.0) continue;
                    // The device's tangent at the limited voltage, carried to the actual one.
                    um[(size_t) (m0 + i)] += g * (vm[(size_t) (v0i + l)] - vEval[(size_t) (v0i + l)]);
                    const Real* mrow = &M[(size_t) (v0i + l) * (size_t) m];
                    for (int k = 0; k < m; ++k)
                        jrow[k] -= g * mrow[k];
                }
            }
            m0 += dm; v0i += dv;
        }
        residualNorm = 0.0;
        for (int j = 0; j < m; ++j)
        {
            F[(size_t) j] = um[(size_t) j] - u[(size_t) j];
            residualNorm += F[(size_t) j] * F[(size_t) j];
        }
    };

    Real norm = 0.0;
    for (int iter = 0; iter < iterations; ++iter)
    {
        evaluateAt (norm);
        if (! std::isfinite (norm))
            return false;
        ++statIterations; ++newtonIterations;
        std::copy (F.begin(), F.end(), du.begin());
        if (affineDevices)
        {
            // While every device stays in its affine part the Jacobian does not change from
            // sample to sample: factor it once and reuse it.
            if (! jacobianFactored || ! std::equal (J.begin(), J.end(), jacobianKey.begin()))
            {
                jacobianKey = J;
                jacobianLu = J;
                luFactor (jacobianLu.data(), jacobianPivot.data(), m);
                jacobianFactored = true;
            }
            luSolve (jacobianLu.data(), jacobianPivot.data(), du.data(), m);
        }
        else
            solveSmall (J.data(), du.data(), m);

        Real biggest = 0.0;
        // With every device affine between where it was linearised and where the step lands,
        // the linearised system is the system there: the step is exact (even when a limit
        // moved the linearisation point, as long as the solution is in the same affine part).
        bool exact = affineDevices;
        for (int i = 0; i < nV; ++i)
        {
            Real dv = 0.0;
            for (int j = 0; j < m; ++j)
                dv += M[(size_t) i * (size_t) m + (size_t) j] * du[(size_t) j];
            biggest = std::max (biggest, std::abs (dv) / std::max (1.0, std::abs (vm[(size_t) i]) * 0.05));
            if (exact)
            {
                const auto& owner = voltageOwner[(size_t) i];
                exact = owner.first->affineBetween (owner.second, vEval[(size_t) i], vm[(size_t) i] + dv);
            }
        }
        if (! std::isfinite (biggest))
            return false;
        for (int j = 0; j < m; ++j)
            u[(size_t) j] += du[(size_t) j];
        // Converged when the step was exact, or small with the devices evaluated where the
        // solution is (no voltage held back by a limit).
        if (exact || (! limited && (biggest < toleranceVolts || biggest < acceptLimit (iter == 0))))
            return true;
    }
    return false;
}

void DeviceGroup::computeOpenVoltages() noexcept
{
    const int np = (int) ports.size();
    const int* start = otherStart.data();
    const int* oq = otherPort.data();
    const Real* orr = otherR.data();
    for (int p = 0; p < np; ++p)
    {
        const auto* net = ports[(size_t) p].net;
        Real v = net->openNodeVoltage (portNode[(size_t) p]);
        for (int k = start[p]; k < start[p + 1]; ++k)
            v += orr[k] * net->portCurrent (oq[k]);
        v0[(size_t) p] = v;
    }
}

void DeviceGroup::solve (bool resweep) noexcept
{
#if AR_PROFILE_OPS
    const auto tp0 = __builtin_readcyclecounter();
#endif
    const int np = (int) ports.size();

    unsigned sum = 0;
    for (const auto& p : ports)
        sum += p.net->refactorCount();
    if (sum != cachedRefactorSum)
    {
        refreshCoupling();
        cachedRefactorSum = sum;
    }
    computeOpenVoltages();

    for (int i = 0; i < nV; ++i)
    {
        Real s = 0.0;
        for (int p = 0; p < np; ++p)
            s += P[(size_t) i * (size_t) np + (size_t) p] * v0[(size_t) p];
        vm0[(size_t) i] = s;
    }

#if AR_PROFILE_OPS
    profPrologue += __builtin_readcyclecounter() - tp0;
    const auto tn0 = __builtin_readcyclecounter();
#endif
    ++statSolves; ++newtonSolves;

    // Start from a linear extrapolation of the last two solutions; if that fails to converge,
    // retry from the last solution with smaller steps.
    if (! resweep)
    {
        std::copy (u.begin(), u.end(), uLast.begin());
        for (int j = 0; j < m; ++j)
            u[(size_t) j] = 3.0 * (u[(size_t) j] - uPrev[(size_t) j]) + uPrev2[(size_t) j];
    }

    bool ok = iterateLimited (maxIterations);
    if (! ok)
    {
        std::copy (uLast.begin(), uLast.end(), u.begin());
        ok = iterate (0.25, maxIterations * 3);
        if (! ok)
        {
            ++statFailures;
            ++failures;
            static int debugLeft = std::getenv ("AR_DEBUG_NEWTON") ? 3 : 0;
            if (debugLeft > 0)
            {
                --debugLeft;
                std::printf ("Newton failure: m=%d nV=%d\n  u:", m, nV);
                for (Real v : u) std::printf (" %.4g", v);
                std::printf ("\n  uLast:");
                for (Real v : uLast) std::printf (" %.4g", v);
                std::printf ("\n  vm0:");
                for (Real v : vm0) std::printf (" %.3f", v);
                std::printf ("\n  vm:");
                for (Real v : vm) std::printf (" %.3f", v);
                std::printf ("\n  F:");
                for (Real v : F) std::printf (" %.4g", v);
                std::printf ("\n  M:");
                for (Real v : M) std::printf (" %.4g", v);
                std::printf ("\n");
            }
            bool finite = true;
            for (Real v : u) finite = finite && std::isfinite (v);
            if (! finite)
                std::copy (uLast.begin(), uLast.end(), u.begin());
        }
    }
    for (int i = 0; i < nV; ++i)
    {
        Real s = vm0[(size_t) i];
        for (int j = 0; j < m; ++j)
            s += M[(size_t) i * (size_t) m + (size_t) j] * u[(size_t) j];
        vSolved[(size_t) i] = s;
    }
    if (! resweep)
    {
        std::copy (uPrev.begin(), uPrev.end(), uPrev2.begin());
        std::copy (uLast.begin(), uLast.end(), uPrev.begin());
    }
#if AR_PROFILE_OPS
    profNewton += __builtin_readcyclecounter() - tn0;
    const auto te0 = __builtin_readcyclecounter();
#endif

    for (int p = 0; p < np; ++p)
    {
        Real i = 0.0;
        for (int j = 0; j < m; ++j)
            i += C[(size_t) p * (size_t) m + (size_t) j] * u[(size_t) j];
        ports[(size_t) p].net->setPortCurrent (ports[(size_t) p].port, i);
    }
#if AR_PROFILE_OPS
    profEpilogue += __builtin_readcyclecounter() - te0;
#endif
}

OpAmpDevice::OpAmpDevice (Real lo, Real hi, PortRef inPlus, PortRef inMinus, PortRef out, Real g, Real r, Real linearFraction)
    : vMin (lo), vMax (hi), gain (g), ro (r), knee (std::clamp (linearFraction, 0.0, 0.99) * 0.5 * (hi - lo))
{
    ports = { inPlus, inMinus, out };
}

void OpAmpDevice::coefficients (Real* C, Real* P, Real*) const noexcept
{
    // ports: 0 in+, 1 in-, 2 out; unknown: current into the out node;
    // model voltages: w = gain * (v+ - v-), the output before the rails, and vout.
    C[2] = 1.0;
    P[0 * 3 + 0] = gain; P[0 * 3 + 1] = -gain;
    P[1 * 3 + 2] = 1.0;
}

void OpAmpDevice::evaluate (const Real* v, Real* u, Real* j) const noexcept
{
    const Real mid = 0.5 * (vMin + vMax), half = 0.5 * (vMax - vMin);
    if (knee > 0.0)
    {
        // Linear to +-knee, then a tanh knee that meets the rail.
        const Real a = std::abs (v[0]);
        Real vo = mid + v[0], slope = 1.0;
        if (a > knee)
        {
            const Real h = half - knee, t = std::tanh ((a - knee) / h);
            vo = mid + std::copysign (knee + h * t, v[0]);
            slope = 1.0 - t * t;
        }
        u[0] = (vo - v[1]) / ro;
        j[0] = slope / ro;
        j[1] = -1.0 / ro;
        return;
    }
    const Real t = std::tanh (v[0] / half);
    const Real vo = mid + half * t;
    u[0] = (vo - v[1]) / ro;
    j[0] = (1.0 - t * t) / ro;
    j[1] = -1.0 / ro;
}

//==============================================================================

} // namespace ar::circuit
