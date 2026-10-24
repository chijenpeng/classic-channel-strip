#pragma once

#include "Module.h"
#include "circuit/Circuit.h"

#include <functional>
#include <memory>
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

    std::vector<ParamSpec> params() const override;

    circuit::Circuit& circuit() noexcept { return *circ; }

protected:
    /** The circuit this module builds into, made afresh by every prepare(). */
    virtual std::unique_ptr<circuit::Circuit> makeCircuit() const { return std::make_unique<circuit::Circuit>(); }
    /** The state after the controls are applied (reset()). */
    virtual void settle();
    /** The controls moved a network: bring the solver up to date (the engine's refactor). */
    virtual void controlsChanged();
    /** Renders one stretch (at most kControlInterval samples) between control updates; `b` is
        the second channel of a stereo module, else null. */
    virtual void renderChunk (float* a, float* b, int numSamples) = 0;
    void processChunks (float* a, float* b, int numSamples);

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

    std::unique_ptr<circuit::Circuit> circ;
    std::vector<Parameter> parameters;
    bool built = false;
    static constexpr int kControlInterval = 32;
    float outputGain = 1.0f;
    int controlCountdown = 0;
};
} // namespace ar::fx
