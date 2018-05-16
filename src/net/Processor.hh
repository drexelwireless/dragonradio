#ifndef PROCESSOR_HH_
#define PROCESSOR_HH_

#include <functional>

#include "net/Element.hh"

using namespace std::placeholders;

/** @brief Packet-processor. */
template <class T>
class Processor : public Element {
public:
    Processor()
      : in(*this, nullptr, nullptr, std::bind(&Processor<T>::push, this, _1))
      , out(*this, nullptr, nullptr)
    {
    }

    virtual ~Processor()
    {
    }

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

#endif /* PROCESSOR_HH_ */
