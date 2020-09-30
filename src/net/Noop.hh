// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef NOOP_HH_
#define NOOP_HH_

#include "net/Processor.hh"

/** @brief A no-op packet-processor. */
template <class T>
class Noop : public Processor<T>
{
public:
    Noop() = default;
    virtual ~Noop() = default;

protected:
    bool process(T &pkt) override
    {
        // Here is where we have a chance to look at a packet and decide whether
        // or not to continue processing it.
        return true;
    }
};

using NetNoop = Noop<std::shared_ptr<NetPacket>>;
using RadioNoop = Noop<std::shared_ptr<RadioPacket>>;

#endif /* NOOP_HH_ */
