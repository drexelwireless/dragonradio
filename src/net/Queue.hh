#ifndef FIFO_HH_
#define FIFO_HH_

#include <functional>

#include "SafeQueue.hh"
#include "SpliceQueue.hh"
#include "net/Element.hh"

using namespace std::placeholders;

/** @brief A (FIFO) queue Element. */
template <class T>
class Queue : public SpliceQueue<T> {
public:
    using const_iterator = typename SpliceQueue<T>::const_iterator;

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


    void push_front(T&& item) override
    {
        queue_.push_front(std::move(item));
    }

    void splice_front(std::list<T>& items)
    {
        queue_.splice_front(items);
    }

    void splice_front(std::list<T>& items, const_iterator first, const_iterator last) override
    {
        queue_.splice_front(items, first, last);
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
