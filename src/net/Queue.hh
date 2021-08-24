// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef QUEUE_HH_
#define QUEUE_HH_

#include <functional>
#include <mutex>

#include "Header.hh"
#include "net/Element.hh"

using namespace std::placeholders;

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
           std::bind(&Queue<T>::push, this, _1))
      , out(*this,
            std::bind(&Queue<T>::reset, this),
            std::bind(&Queue<T>::stop, this),
            std::bind(&Queue<T>::pop, this, _1),
            std::bind(&Queue<T>::kick, this))
    {
    }

    virtual ~Queue() = default;

    /** @brief Reset queue to empty state. */
    virtual void reset(void) = 0;

    /** @brief Push an element onto the queue. */
    virtual void push(T&& val) = 0;

    /** @brief Pop an element from the queue. */
    virtual bool pop(T& val) = 0;

    /** @brief Kick the queue. */
    virtual void kick(void) = 0;

    /** @brief Stop processing queue elements. */
    virtual void stop(void) = 0;

    /** @brief The queue's packet input port. */
    Port<In, Push, T> in;

    /** @brief The queue's packet output port. */
    Port<Out, Pull, T> out;
};

using NetQueue = Queue<std::shared_ptr<NetPacket>>;

#endif /* QUEUE_HH_ */
