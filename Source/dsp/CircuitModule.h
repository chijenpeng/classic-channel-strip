#pragma once

#include "Module.h"
#include "circuit/Circuit.h"

#include <functional>
#include <vector>

namespace ar::fx
{
/** Common machinery for a schematic-driven module: parameter smoothing, sub-block refactoring,
    and the operating-point start. Subclasses build the circuit in build(). */
class CircuitModule : public Module
{
public:
    void prepare (double sampleRate) override;
    void reset() override;
    void setParam (int index, float value) override;
    void process (float* samples, int numSamples) override;

    /** Solve each network's devices as one group (see Circuit::setMergeGroups). */
    virtual bool mergeGroups() const { return false; }
    std::vector<ParamSpec> params() const override;

    circuit::Circuit& circuit() noexcept { return circ; }

protected:
    /** Subclasses add parameters and build the circuit here. */
    virtual void build (double sampleRate) = 0;

    /** Declares a parameter and what it does. Returns its index. */
    int addParam (ParamSpec spec, std::function<void (float)> apply, bool smooth = true);

    struct Parameter
    {
        ParamSpec spec;
        std::function<void (float)> apply;
        float target, current;
        bool smooth;
    };

    circuit::Circuit circ;
    std::vector<Parameter> parameters;
    bool built = false;
    static constexpr int kControlInterval = 32;
    float outputGain = 1.0f;
    int controlCountdown = 0;
};
} // namespace ar::fx
