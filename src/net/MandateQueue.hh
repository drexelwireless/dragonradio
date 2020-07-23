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
      , bonus_phase_(false)
      , hiq_(kHiQueuePriority, FIFO)
      , defaultq_(kDefaultQueuePriority, FIFO)
      , nitems_(0)
      , need_sort_(false)
      , bonus_idx_(0)
    {
        activate_queue(hiq_);
        activate_queue(defaultq_);
    }

    virtual ~MandateQueue()
    {
        stop();
    }

    /** @brief Get flag indicating whether or not to have a bonus phase */
    bool getBonusPhase(void) const
    {
        return bonus_phase_;
    }

    /** @brief Set flag indicating whether or not to have a bonus phase */
    void setBonusPhase(bool bonus_phase)
    {
        bonus_phase_ = bonus_phase;
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
                    need_sort_ = true;
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

        // Deactivate queues that have a mandate but aren't in the new set of
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
            if (it->second.mandate && mandates.find(it->first) == mandates.end()) {
                // Append the items in the queue we are deleting to the end of
                // the default queue.
                defaultq_.splice(defaultq_.end(), it->second);

                // Delete the queue that not longer has a mandate.
                it = flow_qs_.erase(it);
            } else
                it++;
        }

        // Make sure we have a queue for each mandated flow with the proper
        // queue type and mandate. If we update a mandate's priority and it is
        // active, we need to re-sort the list of active queues.
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
                    need_sort_ |= it->second.updatePriority();
                else
                    it->second.updatePriority();
            }
        }

        // Look in the default queue for packets that arrived before the mandate
        // was specified. They need to be re-inserted in the correct queue.
        for (auto it = defaultq_.begin(); it != defaultq_.end(); ) {
            if ((*it)->flow_uid) {
                auto mandate = mandates.find(*(*it)->flow_uid);

                if (mandate != mandates.end()) {
                    // Remove packet from the default queue
                    std::shared_ptr<NetPacket> pkt = std::move(*it);

                    it = erase(defaultq_, pkt, it);

                    // Update packet deadline
                    if (mandate->second.mandated_latency)
                        pkt->deadline = pkt->timestamp + *mandate->second.mandated_latency;

                    // Place packet in the correct queue
                    emplace_back(queue_for(pkt), std::move(pkt));
                } else
                    it++;
            } else
                it++;
        }

        // Record mandates
        mandates_ = mandates;
    }

    using QueuePriorities = std::vector<std::tuple<std::optional<FlowUID>, Priority, std::optional<double>, std::optional<unsigned>, std::optional<unsigned>>>;

    /** @brief Get queue priorities */
    QueuePriorities getQueuePriorities(void)
    {
        std::lock_guard<std::mutex> lock(m_);
        QueuePriorities             result;

        for (auto&& it : qs_) {
            SubQueue &subq = it.get();

            if (subq.mandate)
                result.push_back({subq.mandate->flow_uid, subq.priority, subq.rate, subq.mandate->point_value, subq.min_throughput});
            else
                result.push_back({std::nullopt, subq.priority, std::nullopt, std::nullopt, std::nullopt});
        }

        return result;
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

            emplace_back(hiq_, std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual void repush(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

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
        bool                  bonus = false;

        if (need_sort_)
            sort_queues();

        // If the bonus flag is true, then we have served all mandated flows and
        // can send "bonus" traffic. We do this in round-robin fashion.
    retry:
        do {
            SubQueue &subq = qs_[idx];

            if (!bonus)
                subq.fillBucket(now);

            switch (subq.qtype) {
                case FIFO:
                {
                    auto it = subq.begin();

                    while (it != subq.end()) {
                        if ((*it)->shouldDrop(now)) {
                            drop(**it);
                            it = erase(subq, it);
                        } else if (canPop(*it)) {
                            if (bonus || subq.bucketHasTokensFor(**it)) {
                                pkt = std::move(*it);
                                erase(subq, pkt, it);

                                if (bonus)
                                    bonus_idx_ = idx+1;

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
                    auto it = subq.rbegin();

                    while (it != subq.rend()) {
                        if ((*it)->shouldDrop(now)) {
                            drop(**it);
                            it = decltype(it){ erase(subq, std::next(it).base()) };
                        } else if (canPop(*it)) {
                            if (bonus || subq.bucketHasTokensFor(**it)) {
                                pkt = std::move(*it);
                                erase(subq, pkt, std::next(it).base());

                                if (bonus)
                                    bonus_idx_ = idx+1;

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

        if (!bonus && bonus_phase_) {
            // Enter the bonus phase
            bonus = true;

            // Ensure starting bonus index is valid
            if (bonus_idx_ >= qs_.size())
                bonus_idx_ = 0;

            // Start (and end) at the bonus index
            idx = end = bonus_idx_;

            // Try again to find a packet
            goto retry;
        }

        return false;
    }

    virtual void stop(void) override
    {
        done_ = true;
        cond_.notify_all();
    }

    virtual void updateMCS(NodeId id, const MCS *mcs) override
    {
        std::unique_lock<std::mutex> lock(m_);
        double                       rate = mcs->getRate();

        node_rates_[id] = rate;

        for (auto&& subqref : qs_) {
            SubQueue &subq = subqref.get();

            if (subq.nexthop == id)
                need_sort_ |= subq.updateRate(rate);
        }
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

        SubQueue() = delete;

        SubQueue(const Priority &priority_,
                 QueueType qtype_)
          : priority(priority_)
          , qtype(qtype_)
          , active(false)
          , nbytes(0)
        {
        }

        SubQueue(const Priority &priority_,
                 QueueType qtype_,
                 const Mandate &mandate_)
          : priority(priority_)
          , qtype(qtype_)
          , active(false)
          , mandate(mandate_)
          , nbytes(0)
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

        /** @brief Bytes in queue */
        size_t nbytes;

        bool operator >(const SubQueue &other) const
        {
            return priority > other.priority;
        }

        /** @brief Return true if this queue handles a throughput mandate flow
         */
        bool isThroughput() const
        {
            return mandate && mandate->isThroughput();
        }

        /** @brief Return true if this queue handles a file transfer mandate
         * flow
         */
        bool isFileTransfer() const
        {
            return mandate && mandate->isFileTransfer();
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

        typename container_type::iterator begin(void)
        {
            return q_.begin();
        }

        typename container_type::iterator end(void)
        {
            return q_.end();
        }

        typename container_type::reverse_iterator rbegin(void)
        {
            return q_.rbegin();
        }

        typename container_type::reverse_iterator rend(void)
        {
            return q_.rend();
        }

        typename container_type::iterator erase(typename container_type::const_iterator pos)
        {
            nbytes -= (*pos)->payload_size;
            return q_.erase(pos);
        }

        typename container_type::iterator erase(const T &pkt, typename container_type::const_iterator pos)
        {
            nbytes -= pkt->payload_size;
            return q_.erase(pos);
        }

        void emplace_front(T &&pkt)
        {
            nbytes += pkt->payload_size;
            q_.emplace_front(std::move(pkt));
        }

        void emplace_back(T &&pkt)
        {
            nbytes += pkt->payload_size;
            q_.emplace_back(std::move(pkt));
        }

        void splice(typename container_type::const_iterator pos, SubQueue& other)
        {
            nbytes += other.nbytes;
            other.nbytes = 0;
            q_.splice(pos, other.q_);
        }

        typename container_type::size_type size() const
        {
            return q_.size();
        }

        void clear()
        {
            nbytes = 0;
            q_.clear();
        }

protected:
        /** @brief The queue. */
        container_type q_;
    };

    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Flag indicating whether or not to have a bonus phase. */
    bool bonus_phase_;

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

    /** @brief Does the list of queues need to be sorted? */
    bool need_sort_;

    /** @brief Queues sorted by priority. */
    Queues qs_;

    /** @brief Position of last-served queue during bonus time */
    unsigned bonus_idx_;

    void drop(const NetPacket &pkt) const
    {
        if (logger)
            logger->logDrop(Clock::now(),
                            pkt.hdr,
                            pkt.ehdr(),
                            pkt.mgen_flow_uid.value_or(0),
                            pkt.mgen_seqno.value_or(0),
                            pkt.mcsidx,
                            pkt.size());
    }

    typename SubQueue::container_type::iterator erase(SubQueue &subq,
                                                      typename SubQueue::container_type::const_iterator pos)
    {
        nitems_--;
        return subq.erase(pos);
    }

    typename SubQueue::container_type::iterator erase(SubQueue &subq,
                                                      const T &pkt,
                                                      typename SubQueue::container_type::const_iterator pos)
    {
        nitems_--;
        return subq.erase(pkt, pos);
    }

    /** @brief Deactivate all queues */
    void deactivate_all_queues(void)
    {
        for (auto&& it : qs_) {
            SubQueue &subq = it.get();

            // Remove the queue's items from the total count
            nitems_ -= subq.size();

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
            nitems_ += subq.size();

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
        logEvent("QUEUE: sort (%lu queues)",
            qs_.size());
        std::stable_sort(qs_.begin(), qs_.end(), std::greater<SubQueue>());
        need_sort_ = false;
    }

    void emplace_front(SubQueue &subq, T &&pkt)
    {
        activate_queue(subq, pkt);

        if (subq.updateBurst(*pkt))
            need_sort_ = true;

        subq.emplace_front(std::move(pkt));
        nitems_++;
    }

    void emplace_back(SubQueue &subq, T &&pkt)
    {
        activate_queue(subq, pkt);

        if (subq.updateBurst(*pkt))
            need_sort_ = true;

        subq.emplace_back(std::move(pkt));
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
