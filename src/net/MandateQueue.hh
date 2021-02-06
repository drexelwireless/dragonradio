// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MANDATEQUEUE_HH_
#define MANDATEQUEUE_HH_

#include <deque>
#include <optional>
#include <unordered_map>

#include "Logger.hh"
#include "TimerQueue.hh"
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
      , kicked_(false)
      , transmission_delay_(0.0)
      , bonus_phase_(false)
      , hiq_(*this, kHiQueuePriority, FIFO)
      , defaultq_(*this, kDefaultQueuePriority, FIFO)
      , nitems_(0)
      , need_sort_(false)
      , bonus_idx_(0)
    {
        timer_queue_.start();
        add_queue(hiq_);
        add_queue(defaultq_);
    }

    virtual ~MandateQueue()
    {
        timer_queue_.stop();
        stop();
    }

    void setSendWindowStatus(NodeId id, bool isOpen) override
    {
        Queue<T>::setSendWindowStatus(id, isOpen);

        // Activate any queues associated with the node whose window just
        // opened.
        if (isOpen) {
            std::lock_guard<std::mutex> lock(m_);

            for (auto it = qs_.begin(); it != qs_.end(); ++it) {
                SubQueue &subq = *it;

                if (subq.nexthop == id)
                    subq.activate();
            }
        }
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

        auto&& [it, inserted] = flow_qs_.emplace(std::piecewise_construct,
                                                    std::forward_as_tuple(flow_uid),
                                                    std::forward_as_tuple(*this, kDefaultFlowQueuePriority, qtype));

        if (inserted)
            add_queue(it->second);
        else
            it->second.qtype = qtype;
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

        auto&& [it, inserted] = flow_qs_.emplace(std::piecewise_construct,
                                                    std::forward_as_tuple(flow_uid),
                                                    std::forward_as_tuple(*this, kDefaultFlowQueuePriority, FIFO));

        if (inserted)
            add_queue(it->second);
        else {
            it->second.priority = priority;
            need_sort_ = true;
        }
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

#ifdef MANDATE_EXPIRE_FLOWS
        // Remove queues that have a mandate but aren't in the new set of
        // mandates
        for (auto it = qs_.begin(); it != qs_.end(); ) {
            SubQueue &subq = *it;

            if (subq.mandate && mandates.find(subq.mandate->flow_uid) == mandates.end())
                it = qs_.erase(it);
            else
                it++;
        }

        // Delete queues that have a mandate but aren't in the new mandate map
        for (auto it = flow_qs_.begin(); it != flow_qs_.end(); ) {
            if (it->second.mandate && mandates.find(it->first) == mandates.end()) {
                // Append the items in the queue we are deleting to the end of
                // the default queue.
                defaultq_.append(it->second);

                // Delete the queue that not longer has a mandate.
                it = flow_qs_.erase(it);
            } else
                it++;
        }
#endif

        // Make sure we have a queue for each mandated flow with the proper
        // queue type and mandate. If we update a mandate's priority and it is
        // active, we need to re-sort the list of active queues.
        for (auto&& [flow_uid, mandate] : mandates) {
            // Always use a FIFO queue
            QueueType qtype = FIFO;

            auto&& [it, inserted] = flow_qs_.emplace(std::piecewise_construct,
                                                     std::forward_as_tuple(flow_uid),
                                                     std::forward_as_tuple(*this, kDefaultFlowQueuePriority, qtype, mandate));

            if (inserted) {
                add_queue(it->second);
            } else {
                it->second.qtype = qtype;
                it->second.setMandate(mandate);
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

                    it = defaultq_.erase(pkt, it);

                    // Place packet in the correct queue
                    queue_for(pkt).emplace_back(std::move(pkt));
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

        flow_qs_.clear();
        qs_.clear();

        hiq_.clear();
        defaultq_.clear();

        add_queue(hiq_);
        add_queue(defaultq_);
    }

    virtual void push(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            queue_for(pkt).emplace_back(std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual void push_hi(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.emplace_back(std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual void repush(T &&pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.emplace_back(std::move(pkt));
        }

        cond_.notify_one();
    }

    virtual bool pop(T &pkt) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || kicked_ || nitems_ > 0; });

        if (kicked_) {
            kicked_.store(false, std::memory_order_release);
            return false;
        }

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

            if (subq.active && subq.pop(pkt, *this, now, bonus)) {
                if (bonus)
                    bonus_idx_ = idx+1;

                return true;
            }

            // If we've completed the bonus phase or if there is no bonus phase
            // and this queue could not produce a packet, then deactivate it.
            if (bonus || !bonus_phase_)
                subq.deactivate();

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

    virtual void kick(void) override
    {
        kicked_.store(true, std::memory_order_release);
        cond_.notify_all();
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
                subq.updateRate(rate);
        }
    }

    virtual void setTransmissionDelay(double t) override
    {
        transmission_delay_ = t;
    }

    virtual double getTransmissionDelay(void) const override
    {
        return transmission_delay_;
    }

protected:
    struct SubQueue;

    using Queues = std::vector<std::reference_wrapper<SubQueue>>;

    struct TokenBucket {
        /** @brief Timestamp of last time bucket was filled */
        MonoClock::time_point last_fill;

        /** @brief Number of tokens (bytes) in bucket */
        double tokens;

        /** @brief Maximum number of tokens (bytes) allowed in bucket */
        double max_tokens;
    };

    struct SubQueue : public TimerQueue::Timer {
        using container_type = std::deque<T>;

        /** @brief Statistics for a single measurement period */
        struct MPStats {
            MPStats()
              : npackets(0)
              , npackets_sent(0)
              , nbytes(0)
              , nbytes_sent(0)
            {}

            /** @brief Number of packets enqueued */
            size_t npackets;

            /** @brief Number of packets sent */
            size_t npackets_sent;

            /** @brief Number of bytes enqueued */
            size_t nbytes;

            /** @brief Number of bytes sent */
            size_t nbytes_sent;
        };

        SubQueue() = delete;

        SubQueue(MandateQueue<T> &mqueue_,
                 const Priority &priority_,
                 QueueType qtype_)
          : priority(priority_)
          , qtype(qtype_)
          , active(false)
          , nbytes(0)
          , mq_(mqueue_)
        {
        }

        SubQueue(MandateQueue<T> &mqueue_,
                 const Priority &priority_,
                 QueueType qtype_,
                 const Mandate &mandate_)
          : priority(priority_)
          , qtype(qtype_)
          , active(false)
          , mandate(mandate_)
          , nbytes(0)
          , mq_(mqueue_)
        {
            // Reserve enough room for 30min worth of entries by default (assuming a
            // measurement period of 1 sec)
            stats_.reserve(30*60);

            if (mandate_.min_throughput_bps)
                min_throughput = *(mandate_.min_throughput_bps)/8;
        }

        virtual ~SubQueue()
        {
            deactivate();
            mq_.timer_queue_.cancel(*this);
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
        std::optional<double> min_throughput;

        /** @brief Token bucket */
        std::optional<TokenBucket> bucket;

        /** @brief Bytes in queue */
        size_t nbytes;

        bool operator >(const SubQueue &other) const
        {
            return priority > other.priority;
        }

        void operator()() override
        {
            std::unique_lock<std::mutex> lock(mq_.m_);

            fillBucket(MonoClock::now());
        }

        /** @brief Activate the queue */
        /** INVARIANT: The MandateQueue lock must be held before calling this
         * method
         */
        void activate(void)
        {
            if (!active) {
                // If the queue has a throughput requirement, use it to set the
                // queue's priority.
                if (nexthop) {
                    auto it = mq_.node_rates_.find(*nexthop);

                    if (it != mq_.node_rates_.end())
                        updateRate(it->second);
                }

                // Add the queue's items to the total count
                mq_.nitems_ += size();

                // And now the queue is active...
                active = true;
            }
        }

        /** @brief Deactivate the queue */
        /** INVARIANT: The MandateQueue lock must be held before calling this
         * method
         */
        void deactivate(void)
        {
            if (active) {
                // Remove the queue's items from the total count
                mq_.nitems_ -= size();

                // And now the queue is inactive...
                active = false;
            }
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
                min_throughput = *(new_mandate.min_throughput_bps)/8;

            updatePriority();
        }

        void updateRate(double rate_)
        {
            rate = rate_;
            updatePriority();
        }

        void updatePriority(void)
        {
            if (rate && min_throughput && mandate) {
                double new_priority = (*rate)*static_cast<double>(mandate->point_value)/static_cast<double>(*min_throughput);

                if (priority.second != new_priority) {
                    priority.second = new_priority;
                    mq_.need_sort_ = true;
                }
            }
        }

        void fillBucket(const MonoClock::time_point &now)
        {
            if (bucket && min_throughput) {
                // Add tokens accrued since last time bucket was filled
                double time_delta = (now - bucket->last_fill).get_real_secs();

                bucket->last_fill = now;
                bucket->tokens += kTokenFactor*time_delta*(*min_throughput);
                bucket->tokens = std::min(bucket->tokens, bucket->max_tokens);

                // Activate this flow if it has queued packets and tokens
                // available. Otherwise, set the fill bucket timer.
                if (!q_.empty() && bucket->tokens > 0)
                    activate();
                else
                    setFillBucketTimer();
            }
        }

        void setFillBucketTimer(void)
        {
            // Set the timer to fire when we'll have enough tokens to send a
            // packet.
            if (bucket->tokens <= 0 && *min_throughput > 0) {
                mq_.timer_queue_.run_in(*this,
                    (1.0 - bucket->tokens)/ *min_throughput);
            }
        }

        bool pop(T &pkt,
                 MandateQueue<T> &mqueue,
                 const MonoClock::time_point &now,
                 bool bonus)
        {
            if (!bonus)
                fillBucket(now);

            switch (qtype) {
                case FIFO:
                {
                    auto it = begin();

                    while (it != end()) {
                        if ((*it)->shouldDrop(now)) {
                            drop(**it);
                            it = erase(it);
                        } else if (shouldSend(*it, bonus) && mqueue.canPop(*it)) {
                            pkt = std::move(*it);
                            erase(pkt, it);
                            goto send;
                        } else
                            it++;
                    }
                }
                break;

                case LIFO:
                {
                    auto it = rbegin();

                    while (it != rend()) {
                        if ((*it)->shouldDrop(now)) {
                            drop(**it);
                            it = decltype(it){ erase(std::next(it).base()) };
                        } else if (shouldSend(*it, bonus) && mqueue.canPop(*it)) {
                            pkt = std::move(*it);
                            erase(pkt, std::next(it).base());
                            goto send;
                        } else
                            it++;
                    }
                }
                break;
            }

            // Set the bucket refill time
            setFillBucketTimer();

            return false;

          send:
            if (mandate && pkt->mp) {
                ++stats_[*pkt->mp].npackets_sent;
                stats_[*pkt->mp].nbytes_sent += pkt->payload_size;
            }

            if (bucket)
                bucket->tokens -= pkt->payload_size;

            return true;
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
            if (active)
                --mq_.nitems_;
            nbytes -= (*pos)->payload_size;
            return q_.erase(pos);
        }

        typename container_type::iterator erase(const T &pkt, typename container_type::const_iterator pos)
        {
            if (active)
                --mq_.nitems_;
            nbytes -= pkt->payload_size;
            return q_.erase(pos);
        }

        void emplace_front(T &&pkt)
        {
            preEmplace(pkt);
            q_.emplace_front(std::move(pkt));
            postEmplace();
        }

        void emplace_back(T &&pkt)
        {
            preEmplace(pkt);
            q_.emplace_back(std::move(pkt));
            postEmplace();
        }

        void append(SubQueue& other)
        {
            nbytes += other.nbytes;
            other.nbytes = 0;
            q_.insert(q_.end(),
                      std::make_move_iterator(other.q_.begin()),
                      std::make_move_iterator(other.q_.end()));

            if (active)
                mq_.nitems_ += other.size();

            if (other.active)
                mq_.nitems_ -= other.size();

            other.q_.resize(0);
        }

        typename container_type::size_type size() const
        {
            return q_.size();
        }

        void clear()
        {
            deactivate();
            nbytes = 0;
            q_.clear();
        }

protected:
        /** @brief The queue. */
        container_type q_;

        /** @brief The mandate queue. */
        MandateQueue<T> &mq_;

        /** @brief Measurement period statistics */
        std::vector<MPStats> stats_;

        void preEmplace(const T &pkt)
        {
            if (mandate) {
                // If the queue has a mandate, set its next hop so we can use
                // node rate information to update the queue's priority.
                nexthop = pkt->hdr.nexthop;

                // Add deadline based on mandate.
                if (mandate->max_latency_s)
                    pkt->deadline = (pkt->wall_timestamp ? WallClock::to_mono_time(*pkt->wall_timestamp) : pkt->timestamp) +
                        *mandate->max_latency_s - mq_.transmission_delay_;
            }

            // If the queue is inactive, activate it if either the queue is
            // empty or if this packet should be sent. If the queue is empty, we
            // need to activate it in case its bucket needs to be filled since
            // and empty queue will not have an active bucket fill timer
            // running. We can't just fill the queue here---we would also need
            // to start the bucket fill timer in case filling the bucket doesn't
            // give enough tokens for this packet to be sent. It's easier to
            // just activate the queue, which will do the right thing.
            if (!active && (q_.empty() || shouldSend(pkt, mq_.bonus_phase_)))
                activate();

            // Account for number of packets and number of bytes
            if (active)
                ++mq_.nitems_;

            nbytes += pkt->payload_size;

            // Update per-MP statistics
            if (mandate && pkt->mp) {
                unsigned mp = *pkt->mp;

                if (mp >= stats_.size())
                    stats_.resize(mp + 1);

                ++stats_[mp].npackets;
                stats_[mp].nbytes += pkt->payload_size;
            }
        }

        void postEmplace()
        {
            updateFileTransferThroughput();
        }

        bool shouldSend(const T &pkt, bool bonus)
        {
            if (bonus)
                return true;

            if (isThroughput() && pkt->mp)
                return stats_[*pkt->mp].nbytes_sent < kTokenFactor*(*min_throughput);

            if (bucket)
                return bucket->tokens > 0;

            return true;
        }

        /** @brief Indicate that a packet has been dropped. */
        void drop(const NetPacket &pkt) const
        {
            if (logger)
                logger->logQueueDrop(WallClock::now(),
                                     pkt.nretrans,
                                     pkt.hdr,
                                     pkt.ehdr(),
                                     pkt.mgen_flow_uid.value_or(0),
                                     pkt.mgen_seqno.value_or(0),
                                     pkt.mcsidx,
                                     pkt.size());
        }

        void updateFileTransferThroughput(void)
        {
            if (isFileTransfer()) {
                auto now = MonoClock::now();

                // Purge any packets that should be dropped
                for (auto it = begin(); it != end(); ) {
                    if ((*it)->shouldDrop(now)) {
                        drop(**it);
                        it = erase(it);
                    } else
                        ++it;
                }

                // If we still have packets to send, update our required
                // throughput
                if (nbytes > 0) {
                    // Update file transfer throughput based on
                    const auto &deadline = (*q_.begin())->deadline;

                    if (deadline && *deadline > now) {
                        double delta = (*deadline - now).get_real_secs();
                        double new_min_throughput = (nbytes - bucket->tokens)/delta;

                        setFileTransferThroughput(std::max(0.0, new_min_throughput));
                    }
                }
            }
        }

        void setFileTransferThroughput(double new_min_throughput)
        {
            min_throughput = new_min_throughput;

            if (!bucket) {
                bucket = TokenBucket { MonoClock::now()
                                     , kTokenFactor*new_min_throughput
                                     , kMaxTokenFactor*new_min_throughput
                                     };
            } else {
                bucket->max_tokens = kMaxTokenFactor*new_min_throughput;
                bucket->tokens = std::min(bucket->tokens, bucket->max_tokens);
            }

            // Update priority since min throughput has changed
            updatePriority();

            // Activate this flow if it has queued packets and tokens available.
            // Otherwise, update the fill bucket timer.
            if (!q_.empty() && bucket->tokens > 0)
                activate();
            else
                setFillBucketTimer();
        }
    };

    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Is the queue being kicked? */
    std::atomic<bool> kicked_;

    /** @brief Packet transmission delay in seconds */
    double transmission_delay_;

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

    /** @brief Timer queue */
    TimerQueue timer_queue_;

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

    /** @brief Add a new queue */
    void add_queue(SubQueue &subq)
    {
        // Insert the queue into the list of queues maintaining ordering by
        // priority
        qs_.insert(std::upper_bound(qs_.begin(), qs_.end(), std::ref(subq), std::greater<SubQueue>()),
                   std::ref(subq));
    }

    void sort_queues(void)
    {
        // We sort queues in order of descending priority, so the
        // highest-priority queue is first. We use a stable sort to prevent
        // re-ordering queues with equal priority because we don't want churn to
        // disrupt a stable flow in favor of an unstable flow of equal priority.
        std::stable_sort(qs_.begin(), qs_.end(), std::greater<SubQueue>());
        need_sort_ = false;
    }

    SubQueue &queue_for(const T &pkt)
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
