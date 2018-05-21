#ifndef FIFO_HH_
#define FIFO_HH_

#include <functional>

#include "SafeQueue.hh"
#include "net/Element.hh"

using namespace std::placeholders;

/** @brief A (FIFO) queue Element. */
template <class T>
class Queue : public Element {
public:
    Queue()
      : in(*this,
           nullptr,
           nullptr,
           std::bind(static_cast<void (SafeQueue<T>::*)(T&&)>(&SafeQueue<T>::push), &queue_, _1))
      , out(*this,
            std::bind(&SafeQueue<T>::reset, &queue_),
            std::bind(&SafeQueue<T>::stop, &queue_),
            std::bind(&SafeQueue<T>::pop, &queue_, _1))
    {
    }

    virtual ~Queue()
    {
    }

    /** @brief The queue's packet input port. */
    Port<In, Push, T> in;

    /** @brief The queue's packet output port. */
    Port<Out, Pull, T> out;

protected:
    /** @brief The queue. */
    SafeQueue<T> queue_;
};

using NetQueue = Queue<std::shared_ptr<NetPacket>>;

using RadioQueue = Queue<std::shared_ptr<RadioPacket>>;

#endif /* FIFO_HH_ */
