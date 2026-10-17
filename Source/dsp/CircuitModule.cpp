#include "CircuitModule.h"

#include <algorithm>
#include <cmath>

namespace ar::fx
{
void CircuitModule::prepare (double sampleRate)
{
    circ = circuit::Circuit();
    std::vector<float> oldTargets;
    oldTargets.reserve (parameters.size());
    for (const auto& parameter : parameters)
        oldTargets.push_back (parameter.target);
    parameters.clear();

    build (sampleRate);
    built = true;

    // Keep the knob positions across a rebuild (sample-rate change).
    for (size_t i = 0; i < parameters.size() && i < oldTargets.size(); ++i)
        parameters[i].target = oldTargets[i];

    circ.setMergeGroups (mergeGroups());
    circ.prepare (sampleRate);
    reset();
}

void CircuitModule::reset()
{
    if (! built)
        return;
    for (auto& parameter : parameters)
    {
        parameter.current = parameter.target;
        parameter.apply (parameter.current);
    }
    circ.refactorIfNeeded();
    circ.findOperatingPoint();
    controlCountdown = 0;
}

int CircuitModule::addParam (ParamSpec spec, std::function<void (float)> apply, bool smooth)
{
    const float initial = spec.def;
    const bool shouldSmooth = smooth && spec.type == ParamSpec::Type::knob;
    parameters.push_back ({ std::move (spec), std::move (apply), initial, initial, shouldSmooth });
    return (int) parameters.size() - 1;
}

std::vector<ParamSpec> CircuitModule::params() const
{
    std::vector<ParamSpec> result;
    result.reserve (parameters.size());
    for (const auto& parameter : parameters)
        result.push_back (parameter.spec);
    return result;
}

void CircuitModule::setParam (int index, float value)
{
    if (index >= 0 && index < (int) parameters.size())
        parameters[(size_t) index].target = value;
}

void CircuitModule::process (float* samples, int numSamples)
{
    if (! built)
        return;

    int i = 0;
    while (i < numSamples)
    {
        if (controlCountdown <= 0)
        {
            bool changed = false;
            for (auto& parameter : parameters)
            {
                const float target = parameter.target;
                float& cur = parameter.current;
                if (cur == target)
                    continue;
                if (parameter.smooth)
                {
                    const float step = (parameter.spec.max - parameter.spec.min) * 0.02f;
                    cur = std::abs (target - cur) <= step ? target : cur + (target > cur ? step : -step);
                }
                else
                {
                    cur = target;
                }
                parameter.apply (cur);
                changed = true;
            }
            if (changed)
                circ.refactorIfNeeded();
            controlCountdown = kControlInterval;
        }

        const int n = std::min (numSamples - i, controlCountdown);
        for (int k = 0; k < n; ++k)
            samples[i + k] = (float) circ.process ((circuit::Real) samples[i + k]) * outputGain;
        i += n;
        controlCountdown -= n;
    }
}
} // namespace ar::fx
