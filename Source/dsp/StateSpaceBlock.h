#pragma once

#include "circuit/LinearNetwork.h"

#include <vector>

namespace ar::fx
{
/** Runs a circuit::StateSpace sample by sample on one or two channels (lanes) that share the
    matrices: one dense matrix-vector product per sample, in double precision, no allocation.

    Samples are interleaved, in[t * lanes + lane]. The model can be replaced between calls (a
    control moved): the states are the capacitors' trapezoidal histories, so a capacitor whose
    companion conductance changed (a pre-warp moved) keeps its voltage and current, exactly as
    the circuit engine's setCapacitorWarp does, and a pot or switch change leaves them as they are. */
class StateSpaceBlock
{
public:
    /** Not for the audio thread: room for `maxStates` states on `lanes` (1 or 2) channels. */
    void prepare (int maxStates, int lanes);
    /** Clears the states (silence); the model stays. */
    void reset() noexcept;
    void setModel (const circuit::StateSpace& model) noexcept;
    void process (const double* in, double* out, int numSamples) noexcept;
    /** Flushes states that decayed to denormals. Call once per block. */
    void flushDenormals() noexcept;
    int numStates() const noexcept { return n; }
    int numLanes() const noexcept { return lanes; }

private:
    template <int L> void runGeneric (const double* in, double* out, int numSamples) noexcept;

    int n = 0, lanes = 1, rows = 0;      // rows: n + 1 (the output), padded to an even number
    std::vector<double> m;               // [column j][row i]: columns 0..n-1 the states, n the input; rows 0..n-1 next state, n the output
    std::vector<double> a2, b2, c2;      // the same, row-major A, B and C, for two lanes
    double d2 = 0.0;
    std::vector<double> now, before;     // the state after the last sample and the one before it, lane-interleaved (rows x lanes)
    std::vector<double> g;
};
} // namespace ar::fx
