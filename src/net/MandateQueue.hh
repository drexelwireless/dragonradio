#ifndef MANDATEQUEUE_HH_
#define MANDATEQUEUE_HH_

#include <optional>
#include <unordered_map>

#include "Logger.hh"
#include "cil/CIL.hh"
#include "net/Queue.hh"

/** @brief A queue that obeys mandates. */
template <class T>
class MandateQueue : public Queue<T> {
public:
    using Queue<T>::canPop;

    /** @brief Priority value */
    /** A priority is a pair, with the first number representing the priority of
     * the "category" to which a queue belongs, and the second number
     * representing the relative priority within the given category of queues.
     * The second number is meant to reflect the "value" of a flow.
     */
    using Priority = std::pair<int, double>;

    /** @brief Priority for high-priority queue */
    static constexpr Priority kHiQueuePriority = Priority(100, 0.0);

    /** @brief Default priority for per-flow queue */
    static constexpr Priority kDefaultFlowQueuePriority = Priority(1, 0.0);

    /** @brief Priority for default queue */
    static constexpr Priority kDefaultQueuePriority = Priority(0, 0.0);

    /** @brief Factor specifying maximum tokens in token bucket relative to
     * throughput requirement.
     */
    static constexpr double kMaxTokenFactor = 2.0;

    /** @brief Factor specifying tokens added to token bucket relative to
     * throughput requirement.
     */
    static constexpr double kTokenFactor = 1.1;

    enum QueueType {
        FIFO = 0,
        LIFO
    };

    MandateQueue()
      : Queue<T>()
      , done_(false)
      , hiq_(kHiQueuePriority, FIFO)
      , defaultq_(kDefaultQueuePriority, FIFO)
      , nitems_(0)
    {
        activate_queue(hiq_);
        activate_queue(defaultq_);
    }

    virtual ~MandateQueue()
    {
        stop();
    }

    /** @brief Get flow queue type */
    QueueType getFlowQueueType(FlowUID flow_uid) const
    {
        std::lock_guard<std::mutex> lock(m_);

        return flow_qs_.at(flow_uid).qtype;
    }

    /** @brief Set flow queue priority */
    void setFlowQueueType(FlowUID flow_uid, QueueType qtype)
    {
        std::lock_guard<std::mutex> lock(m_);
        auto                        it = flow_qs_.find(flow_uid);

        if (it != flow_qs_.end())
            it->second.qtype = qtype;
        else
            flow_qs_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(flow_uid),
                             std::forward_as_tuple(kDefaultFlowQueuePriority, qtype));
    }

    /** @brief Get flow queue priority */
    Priority getFlowQueuePriority(FlowUID flow_uid) const
    {
        std::lock_guard<std::mutex> lock(m_);

        return flow_qs_.at(flow_uid).priority;
    }

    /** @brief Set flow queue priority */
    void setFlowQueuePriority(FlowUID flow_uid, const Priority &priority)
    {
        std::lock_guard<std::mutex> lock(m_);
        auto                        it = flow_qs_.find(flow_uid);

        if (it != flow_qs_.end()) {
            SubQueue &subq = it->second;

            if (subq.priority != priority) {
                subq.priority = priority;
                if (subq.active)
                    sort_queues();
            }
        } else
            flow_qs_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(flow_uid),
                             std::forward_as_tuple(priority, FIFO));

    }

    /** @brief Get mandates */
    MandateMap getMandates(void)
    {
        std::lock_guard<std::mutex> lock(m_);

        return mandates_;
    }

    /** @brief Set mandates */
    void setMandates(const MandateMap &mandates)
    {
        std::lock_guard<std::mutex> lock(m_);

        // Deactive queues that have a mandate but aren't in the new set of
        // mandates
        for (auto it = qs_.begin(); it != qs_.end(); ) {
            SubQueue &subq = *it;

            if (subq.mandate && mandates.find(subq.mandate->flow_uid) == mandates.end()) {
                subq.active = false;
                it = qs_.erase(it);
            } else
                it++;
        }

        // Delete queues that have a mandate but aren't in the new mandate map
        for (auto it = flow_qs_.begin(); it != flow_qs_.end(); ) {
            if (it->second.mandate && mandates.find(it->first) == mandates.end())
                it = flow_qs_.erase(it);
            else
                it++;
        }

        // Make sure we have a queue for each mandated flow with the proper
        // queue type and mandate. If we update a mandate's priority and it is
        // active, we need to re-sort the list of active queues.
        bool need_sort = false;

        for (auto&& [flow_uid, mandate] : mandates) {
            // If this is a file transfer mandate, use a FIFO, otherwise, use a
            // LIFO
            QueueType qtype = mandate.file_transfer_deadline_s ? FIFO : LIFO;

            auto&& [it, inserted] = flow_qs_.emplace(std::piecewise_construct,
                                                     std::forward_as_tuple(flow_uid),
                                                     std::forward_as_tuple(kDefaultFlowQueuePriority, qtype, mandate));

            if (!inserted) {
                it->second.qtype = qtype;
                it->second.setMandate(mandate);

                if (it->second.active)
                    need_sort |= it->second.updatePriority();
                else
                    it->second.updatePriority();
            }
        }

        if (need_sort)
            sort_queues();

        // Record mandates
        mandates_ = mandates;
    }

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);

        done_ = false;

        deactivate_all_queues();

        flow_qs_.clear();

        hiq_.clear();
        defaultq_.clear();

        activate_queue(hiq_);
        activate_queue(defaultq_);
    }

    virtual void push(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            emplace_back(queue_for(pkt), std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual void push_hi(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            emplace_front(hiq_, std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual void repush(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            if (pkt->hdr.flags.syn)
                emplace_front(hiq_, std::move(pkt));
            else
                emplace_back(hiq_, std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual bool pop(T &pkt) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || nitems_ > 0; });

        // If we're done, we're done
        if (done_)
            return false;

        MonoClock::time_point now = MonoClock::now();
        unsigned              idx = 0;
        unsigned              end = 0;

        do {
            SubQueue                          &subq = qs_[idx];
            typename SubQueue::container_type &q = subq.q;

            subq.fillBucket(now);

            switch (subq.qtype) {
                case FIFO:
                {
                    auto it = q.begin();

                    while (it != q.end()) {
                        if ((*it)->shouldDrop(now)) {
                            it = erase(subq, it);
                        } else if (canPop(*it)) {
                            if (subq.bucketHasTokensFor(**it)) {
                                pkt = std::move(*it);
                                erase(subq, it);
                                return true;
                            } else
                                break;
                        } else
                            it++;
                    }
                }
                break;

                case LIFO:
                {
                    auto it = q.rbegin();

                    while (it != q.rend()) {
                        if ((*it)->shouldDrop(now)) {
                            it = decltype(it){ erase(subq, std::next(it).base()) };
                        } else if (canPop(*it)) {
                            if (subq.bucketHasTokensFor(**it)) {
                                pkt = std::move(*it);
                                erase(subq, std::next(it).base());
                                return true;
                            } else
                                break;
                        } else
                            it++;
                    }
                }
                break;
            }

            if (++idx == qs_.size())
                idx = 0;
        } while (idx != end);

        return false;
    }

    virtual void stop(void) override
    {
        done_ = true;
        cond_.notify_all();
    }

    virtual void updateTXParams(NodeId id, const TXParams &tx_params) override
    {
        std::unique_lock<std::mutex> lock(m_);
        double                       rate = tx_params.mcs.getRate();
        bool                         need_sort = false;

        node_rates_[id] = rate;

        for (auto&& subqref : qs_) {
            SubQueue &subq = subqref.get();

            if (subq.nexthop == id)
                need_sort |= subq.updateRate(rate);
        }

        if (need_sort)
            sort_queues();
    }

protected:
    struct SubQueue;

    using Queues = std::vector<std::reference_wrapper<SubQueue>>;

    struct Burst {
        Burst() : size(0), interarrival(10) {}

        /** @brief Timestamp of first packet received in burst */
        std::optional<MonoClock::time_point> start;

        /** @brief Timestamp of last packet received in burst */
        MonoClock::time_point last;

        /** @brief Number of payload bytes in burst */
        size_t size;

        /** @brief Burst interarrival time */
        double interarrival;
    };

    struct TokenBucket {
        /** @brief Timestamp of last time bucket was filled */
        std::optional<MonoClock::time_point> last_fill;

        /** @brief Number of tokens (bytes) in bucket */
        double tokens;

        /** @brief Maximum number of tokens (bytes) allowed in bucket */
        double max_tokens;
    };

    struct SubQueue {
        using container_type = std::list<T>;

        SubQueue()
          : priority(kDefaultFlowQueuePriority)
          , qtype(FIFO)
          , active(false)
        {

        }

        SubQueue(const Priority &priority_,
                 QueueType qtype_)
          : priority(priority_)
          , qtype(qtype_)
          , active(false)
        {
        }

        SubQueue(const Priority &priority_,
                 QueueType qtype_,
                 const Mandate &mandate_)
          : priority(priority_)
          , qtype(qtype_)
          , active(false)
          , mandate(mandate_)
        {
            if (mandate_.min_throughput_bps)
                updateMinThroughputBps(*(mandate_.min_throughput_bps)/8);

            // If this is a file transfer mandate, record bursts
            if (mandate_.file_transfer_deadline_s)
                burst = Burst();
        }

        /** @brief Queue priority. */
        Priority priority;

        /** @brief Queue type. */
        QueueType qtype;

        /** @brief The queue. */
        container_type q;

        /** @brief Is this queue active? */
        bool active;

        /** @brief Associated mandate */
        std::optional<Mandate> mandate;

        /** @brief Next hop */
        std::optional<NodeId> nexthop;

        /** @brief Encoding rate */
        std::optional<double> rate;

        /** @brief Minimum throughput (bytes per second) */
        std::optional<size_t> min_throughput;

        /** @brief Token bucket */
        std::optional<TokenBucket> bucket;

        /** @brief Burst counter */
        std::optional<Burst> burst;

        bool operator >(const SubQueue &other) const
        {
            return priority > other.priority;
        }

        void setMandate(const Mandate &new_mandate)
        {
            mandate = new_mandate;

            if (new_mandate.min_throughput_bps)
                updateMinThroughputBps(*(new_mandate.min_throughput_bps)/8);
        }

        bool bucketHasTokensFor(const Packet &pkt)
        {
            if (bucket) {
                if (bucket->tokens >= pkt.payload_size) {
                    bucket->tokens -= pkt.payload_size;
                    return true;
                } else
                    return false;
            } else
                return true;
        }

        bool updatePriority(void)
        {
            if (rate && min_throughput && mandate) {
                double new_priority = (*rate)*static_cast<double>(mandate->point_value)/static_cast<double>(*min_throughput);

                if (priority.second != new_priority) {
                    priority.second = new_priority;
                    return true;
                }
            }

            return false;
        }

        bool updateRate(double rate_)
        {
            rate = rate_;
            return updatePriority();
        }

        bool updateMinThroughputBps(size_t new_min_throughput)
        {
            double old_min_throughput = min_throughput.value_or(0);

            min_throughput = new_min_throughput;

            if (!bucket) {
                bucket = TokenBucket{std::nullopt, 0, kMaxTokenFactor*new_min_throughput};
            } else {
                bucket->max_tokens = kMaxTokenFactor*new_min_throughput;

                if (bucket->last_fill) {
                    if (new_min_throughput > old_min_throughput) {
                        double new_tokens = kTokenFactor*(new_min_throughput - old_min_throughput);

                        bucket->tokens += new_tokens;
                    }

                    bucket->tokens = std::min(bucket->tokens, bucket->max_tokens);
                }
            }

            return updatePriority();
        }

        void fillBucket(const MonoClock::time_point &now)
        {
            if (bucket && min_throughput) {
                double new_tokens;

                if (!bucket->last_fill) {
                    new_tokens = kTokenFactor*(*min_throughput);

                    bucket->last_fill = now;
                } else {
                    double time_delta = (now - *(bucket->last_fill)).get_real_secs();

                    new_tokens = kTokenFactor*time_delta*(*min_throughput);
                }

                bucket->last_fill = now;
                bucket->tokens += new_tokens;
                bucket->tokens = std::min(bucket->tokens, bucket->max_tokens);
            }
        }

        bool updateBurst(const Packet &pkt)
        {
            if (burst) {
                auto &ts = pkt.timestamp;

                if (burst->start)
                    assert(ts > *burst->start);

                // If this is the end of a burst, update the interarrival time
                // and the throughput, and reinitialize the burst state.
                if (burst->start && (ts - burst->last).get_full_secs() > 0) {
                    burst->interarrival = (ts - *burst->start).get_full_secs();

                    double burst_throughput = burst->size/burst->interarrival;

                    burst->start = ts;
                    burst->last = ts;
                    burst->size = pkt.payload_size;

                    return updateMinThroughputBps(burst_throughput);
                } else {
                    // If this is the first burst, initialize the burst state
                    if (!burst->start) {
                        burst->start = ts;
                        burst->size = 0;
                    }

                    // Update burst for this packet
                    burst->last = ts;
                    burst->size += pkt.payload_size;

                    // Update the burst throughput requirement if it has
                    // increased
                    double burst_throughput = burst->size/burst->interarrival;

                    if (!min_throughput || burst_throughput > *min_throughput)
                        return updateMinThroughputBps(burst_throughput);
                }
            }

            return false;
        }

        void clear()
        {
            return q.clear();
        }
    };

    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Mutex protecting the queues. */
    mutable std::mutex m_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief Mandates */
    MandateMap mandates_;

    /** @brief The high-priority queue. */
    SubQueue hiq_;

    /** @brief The default queue. */
    SubQueue defaultq_;

    /** @brief Number of enqueued items. */
    unsigned nitems_;

    /** @brief Map from node ID to rate. */
    std::unordered_map<NodeId, double> node_rates_;

    /** @brief Map from flow to queue. */
    std::unordered_map<FlowUID, SubQueue> flow_qs_;

    /** @brief Queues sorted by priority. */
    Queues qs_;

    typename SubQueue::container_type::iterator erase(SubQueue &subq,
                                                      typename SubQueue::container_type::iterator pos)
    {
        nitems_--;
        return subq.q.erase(pos);
    }

    /** @brief Deactivate all queues */
    void deactivate_all_queues(void)
    {
        for (auto&& it : qs_) {
            SubQueue &subq = it.get();

            // Remove the queue's items from the total count
            nitems_ -= subq.q.size();

            // Mark the queue as not active
            subq.active = false;
        }

        qs_.clear();
    }

    /** @brief Activate a queue */
    void activate_queue(SubQueue &subq)
    {
        // If this queue has not been made active yet, make it active now
        if (!subq.active) {
            // If the queue has a throughput requirement, use it to set the
            // queue's priority.
            if (subq.nexthop) {
                auto it = node_rates_.find(*subq.nexthop);

                if (it != node_rates_.end())
                    subq.updateRate(it->second);
            }

            // Insert the queue into the list of queues maintaining ordering by
            // priority
            qs_.insert(std::upper_bound(qs_.begin(), qs_.end(), std::ref(subq), std::greater<SubQueue>()),
                       std::ref(subq));

            // Add the queue's items to the total count
            nitems_ += subq.q.size();

            // And now the queue is active...
            subq.active = true;
        }
    }

    /** @brief Activate a queue, specifying next packet to send */
    void activate_queue(SubQueue &subq, const T &pkt)
    {
        // If this queue has not been made active yet, make it active now
        if (!subq.active) {
            // If the queue has a mandate, set its nexthop so we can update its
            // priority later
            if (subq.mandate)
                subq.nexthop = pkt->hdr.nexthop;

            activate_queue(subq);
        }
    }

    void sort_queues(void)
    {
        // We sort queues in order of descending priority, so the
        // highest-priority queue is first. We use a stable sort to prevent
        // re-ordering queues with equal priority because we don't want churn to
        // disrupt a stable flow in favor of an unstable flow of equal priority.
        std::stable_sort(qs_.begin(), qs_.end(), std::greater<SubQueue>());
    }

    void emplace_front(SubQueue &subq, T &&pkt)
    {
        activate_queue(subq, pkt);

        if (subq.updateBurst(*pkt))
            sort_queues();

        subq.q.emplace_front(std::move(pkt));
        nitems_++;
    }

    void emplace_back(SubQueue &subq, T &&pkt)
    {
        activate_queue(subq, pkt);

        if (subq.updateBurst(*pkt))
            sort_queues();

        subq.q.emplace_back(std::move(pkt));
        nitems_++;
    }

    SubQueue &queue_for(T &pkt)
    {
        if (pkt->flow_uid) {
            auto it = flow_qs_.find(*pkt->flow_uid);

            if (it != flow_qs_.end())
                return it->second;
        }

        return defaultq_;
    }
};

using MandateNetQueue = MandateQueue<std::shared_ptr<NetPacket>>;

#endif /* MANDATEQUEUE_HH_ */
