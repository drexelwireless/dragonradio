#ifndef QUEUE_HH_
#define QUEUE_HH_

#include <functional>

#include "spinlock_mutex.hh"
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
            std::bind(&Queue<T>::pop, this, _1))
    {
    }

    virtual ~Queue() = default;

    /** @brief Reset queue to empty state. */
    virtual void reset(void) = 0;

    /** @brief Push an element onto the queue. */
    virtual void push(T&& val) = 0;

    /** @brief Push an element onto the high-priority queue. */
    virtual void push_hi(T&& item) = 0;

    /** @brief Re-queue and element. */
    virtual void repush(T&& item) = 0;

    /** @brief Pop an element from the queue. */
    virtual bool pop(T& val) = 0;

    /** @brief Stop processing queue elements. */
    virtual void stop(void) = 0;

    /** @brief Notify queue of new MCS */
    virtual void updateMCS(NodeId id, const MCS *mcs) = 0;

    /** @brief Set whether or not a node's send window is open */
    virtual void setSendWindowStatus(NodeId id, bool isOpen)
    {
        std::lock_guard<spinlock_mutex> lock(send_window_status_mutex_);

        send_window_status_[id] = isOpen;
    }

    /** @brief The queue's packet input port. */
    Port<In, Push, T> in;

    /** @brief The queue's packet output port. */
    Port<Out, Pull, T> out;

protected:
    /** @brief Mutex protecting the send window status */
    spinlock_mutex send_window_status_mutex_;

    /** @brief Nodes' send window statuses */
    std::unordered_map<NodeId, bool> send_window_status_;

    /** @brief Return true if the packet can be popped */
    bool canPop(const T& pkt)
    {
        if (pkt->hdr.nexthop == kNodeBroadcast || pkt->internal_flags.has_seq)
            return true;

        std::lock_guard<spinlock_mutex> lock(send_window_status_mutex_);
        auto                            it = send_window_status_.find(pkt->hdr.nexthop);

        if (it != send_window_status_.end())
            return it->second;
        else
            return true;
    }
};

using NetQueue = Queue<std::shared_ptr<NetPacket>>;

#endif /* QUEUE_HH_ */
