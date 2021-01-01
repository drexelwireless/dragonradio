// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef PROCESSOR_HH_
#define PROCESSOR_HH_

#include <functional>

#include "net/Element.hh"

/** @brief Packet-processor. */
template <class T>
class Processor : public Element {
public:
    Processor()
      : in(*this,
           nullptr,
           nullptr,
           std::bind(&Processor<T>::push, this, std::placeholders::_1))
      , out(*this,
            nullptr,
            nullptr)
    {
    }

    virtual ~Processor() = default;

    /** @brief The processor's packet input port. */
    Port<In,Push,T> in;

    /** @brief The processor's packet output port. */
    Port<Out,Push,T> out;

protected:
    void push(T&& pkt)
    {
        if (process(pkt))
            out.push(std::move(pkt));
    }

    virtual bool process(T& pkt) = 0;
};

using NetProcessor = Processor<std::shared_ptr<NetPacket>>;
using RadioProcessor = Processor<std::shared_ptr<RadioPacket>>;

#endif /* PROCESSOR_HH_ */
