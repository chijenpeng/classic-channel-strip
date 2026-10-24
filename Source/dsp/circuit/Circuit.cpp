#include "Circuit.h"

#include <stdexcept>

namespace ar::circuit
{
LinearNetwork& Circuit::addNetwork (const std::string& name)
{
    nets.push_back (std::make_unique<LinearNetwork> (name));
    return *nets.back();
}

int Circuit::indexOf (const LinearNetwork& net) const
{
    for (size_t i = 0; i < nets.size(); ++i)
        if (nets[i].get() == &net)
            return (int) i;
    throw std::logic_error ("network is not part of this circuit");
}

void Circuit::prepare (Real sampleRate)
{
    for (auto& net : nets)
        net->prepare (sampleRate);
}

void Circuit::refactorIfNeeded()
{
    for (auto& net : nets)
        net->refactorIfNeeded();
}
} // namespace ar::circuit
