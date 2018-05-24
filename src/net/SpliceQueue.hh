#ifndef SPLICEQUEUE_HH_
#define SPLICEQUEUE_HH_

#include <functional>
#include <list>

#include "Packet.hh"
#include "SafeQueue.hh"
#include "net/Element.hh"

using namespace std::placeholders;

/** @brief A queue Element that allows items to be spliced at the fron of the
 * queue.
 */
template <class T>
class SpliceQueue : public Element {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    SpliceQueue() = default;
    virtual ~SpliceQueue() = default;

    virtual void push_front(T&& item) = 0;
    virtual void splice_front(std::list<T>& items) = 0;
    virtual void splice_front(std::list<T>& items,
                              const_iterator first,
                              const_iterator last) = 0;
};

using NetSpliceQueue = SpliceQueue<std::shared_ptr<NetPacket>>;

using RadioSpliceQueue = SpliceQueue<std::shared_ptr<RadioPacket>>;

#endif /* FIFO_HH_ */
