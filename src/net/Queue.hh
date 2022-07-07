// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef QUEUE_HH_
#define QUEUE_HH_

#include <functional>
#include <mutex>

#include "Header.hh"
#include "net/Element.hh"

/** @brief A queue Element that has a separate high-priority queue that is
 * always serviced first.
 */
template <class T>
class Queue : public Element {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    Queue()
      : in(*this,
           nullptr,
           nullptr,
           std::bind(&Queue<T>::push, this, std::placeholders::_1))
      , out(*this,
            std::bind(&Queue<T>::connected, this),
            std::bind(&Queue<T>::disconnected, this),
            std::bind(&Queue<T>::enable, this),
            std::bind(&Queue<T>::disable, this),
            std::bind(&Queue<T>::pop, this, std::placeholders::_1))
    {
    }

    virtual ~Queue() = default;

    /** @brief Is the queue enabled? */
    virtual bool isEnabled(void) const = 0;

    /** @brief Enable the queue. */
    virtual void enable(void) = 0;

    /** @brief Disable the queue. */
    virtual void disable(void) = 0;

    /** @brief Queue size. */
    virtual size_t size(void) = 0;

    /** @brief Reset queue to empty state. */
    virtual void reset(void) = 0;

    /** @brief Push an element onto the queue. */
    virtual void push(T&& val) = 0;

    /** @brief Pop an element from the queue. */
    virtual bool pop(T& val) = 0;

    /** @brief The queue's packet input port. */
    Port<In, Push, T> in;

    /** @brief The queue's packet output port. */
    Port<Out, Pull, T> out;

  protected:
    /** @brief Called when queue is connected */
    virtual void connected(void)
    {
        reset();
    }

    /** @brief Called when queue is disconnected */
    virtual void disconnected(void)
    {
        disable();
    }
};

using NetQueue = Queue<std::shared_ptr<NetPacket>>;

#endif /* QUEUE_HH_ */
