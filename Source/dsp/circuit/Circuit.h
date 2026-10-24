#pragma once

#include "LinearNetwork.h"

#include <memory>
#include <string>
#include <vector>

namespace ar::circuit
{
/** The linear networks that make up a card, owned in the order they were added. The card model
    extracts each network's state space (LinearNetwork::extractStateSpace) and runs it itself. */
class Circuit
{
public:
    virtual ~Circuit() = default;

    LinearNetwork& addNetwork (const std::string& name);
    int indexOf (const LinearNetwork&) const;

    /** Prepares every network for a sample rate. */
    virtual void prepare (Real sampleRate);
    void refactorIfNeeded();

    std::vector<std::unique_ptr<LinearNetwork>>& networks() noexcept { return nets; }

protected:
    std::vector<std::unique_ptr<LinearNetwork>> nets;
};
} // namespace ar::circuit
