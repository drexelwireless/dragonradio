#ifndef FIFO_HH_
#define FIFO_HH_

#include "net/Queue.hh"

/** @brief A FIFO queue Element. */
template <class T>
class FIFO : public SimpleQueue<T> {
public:
    using SimpleQueue<T>::canPop;
    using SimpleQueue<T>::done_;
    using SimpleQueue<T>::m_;
    using SimpleQueue<T>::cond_;
    using SimpleQueue<T>::hiq_;
    using SimpleQueue<T>::q_;

    FIFO() = default;

    virtual ~FIFO() = default;

    virtual bool pop(T& val) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || !hiq_.empty() || !q_.empty(); });

        // If we're done, we're done
        if (done_)
            return false;

        MonoClock::time_point now = MonoClock::now();

        // First look in high-priority queue
        {
            auto it = hiq_.begin();

            while (it != hiq_.end()) {
                if ((*it)->shouldDrop(now))
                    it = hiq_.erase(it);
                else if (canPop(*it)) {
                    val = std::move(*it);
                    hiq_.erase(it);
                    return true;
                } else
                    it++;
            }
        }

        // Then look in the network queue, FIFO-style
        {
            auto it = q_.begin();

            while (it != q_.end()) {
                if ((*it)->shouldDrop(now))
                    it = q_.erase(it);
                else if (canPop(*it)) {
                    val = std::move(*it);
                    q_.erase(it);
                    return true;
                } else
                    it++;
            }
        }

        return false;
    }
};

using NetFIFO = FIFO<std::shared_ptr<NetPacket>>;

using RadioFIFO = FIFO<std::shared_ptr<RadioPacket>>;

#endif /* FIFO_HH_ */
