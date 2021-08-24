// Copyright 2018-2021 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SMARTCONTROLLER_H_
#define SMARTCONTROLLER_H_

#include <sys/types.h>
#include <netinet/if_ether.h>

#include <list>
#include <map>
#include <random>

#include "heap.hh"
#include "Clock.hh"
#include "TimerQueue.hh"
#include "llc/Controller.hh"
#include "mac/MAC.hh"
#include "phy/Gain.hh"
#include "phy/PHY.hh"
#include "stats/Estimator.hh"
#include "stats/TimeWindowEstimator.hh"

class SmartController;

/** @brief Timestamps associated with a node */
struct Timestamps {
    /** @brief Map from timestamp sequence number to timestamp. */
    using timestamp_map = std::unordered_map<TimestampSeq, MonoClock::time_point>;

    /** @brief Map from timestamp sequence number to pair of sent, received timestamps. */
    using timestamps_map = std::unordered_map<TimestampSeq, std::pair<MonoClock::time_point, MonoClock::time_point>>;

    /** @brief Vector of pairs of timestamps. */
    using timestampseq_set = std::unordered_set<TimestampSeq>;

    /** @brief Timestamp sequences sent by node */
    timestamp_map timestamps_sent;

    /** @brief Timestamp sequences received from node */
    timestamp_map timestamps_recv;

    /** @brief Echoed timestamp sequences */
    timestampseq_set timestamps_echoed;

    /** @brief Timestamps received from this node */
    timestamps_map timestamps;
};

struct SendWindow {
    struct Entry;

    using vector_type = std::vector<Entry>;

    SendWindow(Node &n,
               SmartController &controller,
               Seq::uint_type maxwin,
               double retransmission_delay_);

    /** @brief Destination node. */
    Node &node;

    /** @brief Our controller. */
    SmartController &controller;

    /** @brief PHY's MCS table */
    std::vector<PHY::MCSEntry> &mcs_table;

    /** @brief Mutex for the send window */
    std::mutex mutex;

    /** @brief Modulation index */
    size_t mcsidx;

    /** @brief Short-term EVM, as reported by receiver */
    std::optional<float> short_evm;

    /** @brief Long-term EVM, as reported by receiver */
    std::optional<float> long_evm;

    /** @brief Short-term RSSI, as reported by receiver */
    std::optional<float> short_rssi;

    /** @brief Long-term RSSI, as reported by receiver */
    std::optional<float> long_rssi;

    /** @brief Is this a new window? */
    bool new_window;

    /** @brief Is the window open? */
    bool window_open;

    /** @brief Current sequence number for this destination */
    Seq seq;

    /** @brief First un-ACKed sequence number. */
    Seq unack;

    /** @brief Maximum sequence number we have sent. */
    /** INVARIANT: max < unack + win */
    Seq max;

    /** @brief Do we need to send a set unack control message? */
    bool send_set_unack;

    /** @brief Send window size */
    Seq::uint_type win;

    /** @brief Maximum window size */
    Seq::uint_type maxwin;

    /** @brief The probability of moving to a given MCS */
    std::vector<double> mcsidx_prob;

    /** @brief First sequence that can possibly be used to calculate PER. */
    /** The packet with this sequence number is the first that can possibly be
      * used to calculate PER. We use this when the environment has changed and
      * previously-sent packets should not be used to calculate PER in the new
      * environment.
     */
    Seq per_cutoff;

    /** @brief End of the current PER window PER. */
    /** Every packet up to, but not including, this sequence number has already been
     * used to calculate the current PER
     */
    Seq per_end;

    /** @brief Previous short-term packet error rate */
    double prev_short_per;

    /** @brief Previous long-term packet error rate */
    double prev_long_per;

    /** @brief Short-term packet error rate */
    WindowedMean<double> short_per;

    /** @brief Long-term packet error rate */
    WindowedMean<double> long_per;

    /** @brief Duration of retransmission timer */
    double retransmission_delay;

    /** @brief ACK delay estimator */
    TimeWindowMax<MonoClock, double> ack_delay;

    /** @brief Return the packet with the given sequence number in the window */
    Entry& operator[](Seq seq)
    {
        return entries_[seq % entries_.size()];
    }

    /** @brief Set send window status */
    void setSendWindowStatus(bool open);

    /** @brief Record a packet ACK */
    void ack(const MonoClock::time_point &tx_time);

    /** @brief Update PER as a result of successful packet transmission. */
    void txSuccess(void);

    /** @brief Update PER as a result of unsuccessful packet transmission. */
    void txFailure(void);

    /** @brief Update MCS based on current PER */
    void updateMCS(bool fast_adjust);

    /** @brief Return true if we may move up one MCS level */
    bool mayMoveUpMCS(void) const;

    /** @brief Move down one MCS level */
    void moveDownMCS(unsigned n)
    {
        setMCS(mcsidx - n);
    }

    /** @brief Move up one MCS level */
    void moveUpMCS(void)
    {
        setMCS(mcsidx + 1);
    }

    /** @brief Set MCS */
    void setMCS(size_t mcsidx);

    /** @brief Reconfigure a node's PER estimates */
    void resetPEREstimates(void);

    struct Entry : public TimerQueue::Timer {
        Entry(SendWindow &sendw)
          : sendw(sendw)
          , pkt(nullptr)
          , timestamp(0.0)
         {
         };

        virtual ~Entry() = default;

        /** @brief Does this entry have a pending packet to be sent? */
        inline bool pending()
        {
            return (bool) pkt;
        }

        /** @brief Set packet in send window entry
         * @param p The packet.
         */
        inline void set(const std::shared_ptr<NetPacket>& p)
        {
            pkt = p;
        }

        /** @brief Get packet in send window entry
         * @return The packet.
         */
        inline std::shared_ptr<NetPacket> get()
        {
            return pkt;
        }

        /** @brief Release packet */
        inline void reset(void)
        {
            pkt.reset();
        }

        /** @brief Return true if we MAY drop this window entry. */
        /** We MAY drop an entry if:
         * 1) It is NOT a SYN packet, because in that case it is needed to
         * initiate a connection. We always retransmit SYN packets.
         */
        inline bool mayDrop(const std::optional<size_t> &max_retransmissions)
        {
            return !pkt->hdr.flags.syn;
        }

        /** @brief Return true if we SHOULD drop this window entry. */
        /** We SHOULD drop an entry if:
         * 1) It is NOT a SYN packet, because in that case it is needed to
         * initiate a connection. We always retransmit SYN packets.
         * AND
         * 2) It has exceeded the maximum number of allowed retransmissions.
         * 3) OR it has passed its deadline.
         */
        inline bool shouldDrop(const std::optional<size_t> &max_retransmissions)
        {
            return !pkt->hdr.flags.syn &&
                 (  (max_retransmissions && pkt->nretrans >= *max_retransmissions)
                 || pkt->deadlinePassed(MonoClock::now()));
        }

        void operator()() override;

        /** @brief The send window. */
        SendWindow &sendw;

        /** @brief The packet received in this window entry. */
        std::shared_ptr<NetPacket> pkt;

        /** @brief Timestamp of last transmission of this packet. */
        /** This is the time at which the packet was queued for transmission,
         * not the actual time at which it was transmitted, which is instead
         * recorded in the packet itself.
         */
        MonoClock::time_point timestamp;
    };

private:
    /** @brief Unacknowledged packets in our send window. */
    /** INVARIANT: unack <= N <= max < unack + win */
    vector_type entries_;
};

struct RecvWindow : public TimerQueue::Timer  {
    struct Entry;

    using vector_type = std::vector<Entry>;

    RecvWindow(Node &n,
               SmartController &controller,
               Seq::uint_type win,
               size_t nak_win);

    /** @brief Sender node. */
    Node &node;

    /** @brief Our controller. */
    SmartController &controller;

    /** @brief Mutex for the receive window */
    std::mutex mutex;

    /** @brief Short-term packet EVM */
    TimeWindowMean<MonoClock, float> short_evm;

    /** @brief Long-term packet EVM */
    TimeWindowMean<MonoClock, float> long_evm;

    /** @brief Short-term packet RSSI */
    TimeWindowMean<MonoClock, float> short_rssi;

    /** @brief Long-term packet RSSI */
    TimeWindowMean<MonoClock, float> long_rssi;

    /** @brief True when this is an active window that has received a packet */
    bool active;

    /** @brief Next sequence number we should ACK. */
    /** We have received (or given up) on all packets with sequence numbers <
     * this number. INVARIANT: The smallest sequence number in our receive
     * window must be STRICTLY > ack because we have already received the packet
     * with sequence number ack - 1, and if we received the packet with
     * sequence number ack, we should have updated ack to ack + 1.
     */
    Seq ack;

    /** @brief Maximum sequence number we have received */
    /** INVARIANT: ack <= max < ack + win. When max == ack, we have no holes in
    * our receive window, which should therefore be empty.
    */
    Seq max;

    /** @brief Timestamp of packet with the maximum sequence number we have
      * sent. */
    MonoClock::time_point max_timestamp;

    /** @brief Receive window size */
    Seq::uint_type win;

    /** @brief Flag indicating whether or not we need a selective ACK. */
    bool need_selective_ack;

    /** @brief Flag indicating whether or not the timer is for an ACK or a
     * selective ACK.
     */
    bool timer_for_ack;

    /** @brief Explicit NAK window */
    std::vector<MonoClock::time_point> explicit_nak_win;

    /** @brief Explicit NAK window index */
    size_t explicit_nak_idx;

    /** @brief Return true if sequence number is in the receive window */
    inline bool contains(Seq seq)
    {
        return seq >= max - win && seq < ack + win;
    }

    /** @brief Reset the receive window */
    void reset(Seq seq);

    /** @brief Return the packet with the given sequence number in the window */
    Entry& operator[](Seq seq)
    {
#if defined(LOGDEBUG)
        assert(seq >= ack && seq <= ack + win && max <= ack + win);
#endif /* defined(LOGDEBUG) */
        return entries_[seq % entries_.size()];
    }

    void operator()() override;

    struct Entry {
        Entry() : received(false), delivered(false), pkt(nullptr) {};

        /** @brief Set packet in receive window entry.
         * @param p The packet.
         */
        inline void set(std::shared_ptr<RadioPacket>&& p)
        {
            received = true;
            delivered = false;
            pkt = std::move(p);
        }

        void alreadyDelivered(void)
        {
            received = true;
            delivered = true;
        }

        void reset(void)
        {
            received = false;
            delivered = false;
            pkt.reset();
        }

        /** @brief Was this entry in the window received? */
        bool received;

        /** @brief Was this entry in the window delivered? */
        bool delivered;

        /** @brief The packet received in this window entry. */
        std::shared_ptr<RadioPacket> pkt;
    };

private:
    /** @brief All packets with sequence numbers N such that
     * ack <= N <= max < ack + win
     */
    vector_type entries_;
};

/** @brief A MAC controller that implements ARQ. */
class SmartController : public Controller
{
    friend struct SendWindow;
    friend struct RecvWindow;

    friend class SendWindowGuard;
    friend class RecvWindowGuard;

public:
    using evm_thresh_t = std::optional<double>;

    SmartController(std::shared_ptr<RadioNet> radionet_,
                    size_t mtu,
                    std::shared_ptr<PHY> phy,
                    double slot_size,
                    Seq::uint_type max_sendwin,
                    Seq::uint_type recvwin,
                    const std::vector<evm_thresh_t> &evm_thresholds);
    virtual ~SmartController();

    /** @brief Get MCS fast adjustment period (sec) */
    double getMCSFastAdjustmentPeriod(void) const
    {
        return mcs_fast_adjustment_period_;
    }

    /** @brief Set MCS fast adjustment period (sec) */
    void setMCSFastAdjustmentPeriod(double t)
    {
        mcs_fast_adjustment_period_ = t;
    }

    /** @brief Are we in MCS fast adjustment period? */
    bool isMCSFastAdjustmentPeriod(void) const
    {
        return env_timestamp_ &&
            (MonoClock::now() - *env_timestamp_).get_real_secs() < mcs_fast_adjustment_period_;
    }

    /** @brief Get short time window over which to calculate PER (sec) */
    double getShortPERWindow(void) const
    {
        return short_per_window_;
    }

    /** @brief Set short time window over which to calculate PER (sec) */
    void setShortPERWindow(double window)
    {
        short_per_window_ = window;
    }

    /** @brief Get long time window over which to calculate PER (sec) */
    double getLongPERWindow(void) const
    {
        return long_per_window_;
    }

    /** @brief Set long time window over which to calculate PER (sec) */
    void setLongPERWindow(double window)
    {
        long_per_window_ = window;
    }

    /** @brief Get time window for statistics collection (sec) */
    double getShortStatsWindow(void) const
    {
        return short_stats_window_;
    }

    /** @brief Set time window for statistics collection (sec) */
    void setShortStatsWindow(double window)
    {
        short_stats_window_ = window;
    }

    /** @brief Get time window for statistics collection (sec) */
    double getLongStatsWindow(void) const
    {
        return long_stats_window_;
    }

    /** @brief Set time window for statistics collection (sec) */
    void setLongStatsWindow(double window)
    {
        long_stats_window_ = window;
    }

    /** @brief Get broadcast MCS index */
    mcsidx_t getBroadcastMCSIndex(void) const
    {
        return mcsidx_broadcast_;
    }

    /** @brief Set broadcast MCS index */
    void setBroadcastMCSIndex(mcsidx_t mcsidx)
    {
        if(mcsidx >= phy_->mcs_table.size())
            throw std::out_of_range("MCS index out of range");

        mcsidx_broadcast_ = mcsidx;
    }

    /** @brief Get ACK MCS index */
    mcsidx_t getAckMCSIndex(void) const
    {
        return mcsidx_ack_;
    }

    /** @brief Set ACK MCS index */
    void setAckMCSIndex(mcsidx_t mcsidx)
    {
        if(mcsidx >= phy_->mcs_table.size())
            throw std::out_of_range("MCS index out of range");

        mcsidx_ack_ = mcsidx;
    }

    /** @brief Get minimum MCS index */
    mcsidx_t getMinMCSIndex(void) const
    {
        return mcsidx_min_;
    }

    /** @brief Set minimum MCS index */
    void setMinMCSIndex(mcsidx_t mcsidx)
    {
        if(mcsidx >= phy_->mcs_table.size())
            throw std::out_of_range("MCS index out of range");

        mcsidx_min_ = mcsidx;
    }

    /** @brief Get maximum MCS index */
    mcsidx_t getMaxMCSIndex(void) const
    {
        return mcsidx_max_;
    }

    /** @brief Set maximum MCS index */
    void setMaxMCSIndex(mcsidx_t mcsidx)
    {
        if(mcsidx >= phy_->mcs_table.size())
            throw std::out_of_range("MCS index out of range");

        mcsidx_max_ = mcsidx;
    }

    /** @brief Get initial MCS index */
    mcsidx_t getInitialMCSIndex(void) const
    {
        return mcsidx_init_;
    }

    /** @brief Set initial MCS index */
    void setInitialMCSIndex(mcsidx_t mcsidx)
    {
        if(mcsidx >= phy_->mcs_table.size())
            throw std::out_of_range("MCS index out of range");

        mcsidx_init_ = mcsidx;
    }

    /** @brief Get PER threshold for increasing modulation level */
    double getUpPERThreshold(void) const
    {
        return mcsidx_up_per_threshold_;
    }

    /** @brief Set PER threshold for increasing modulation level */
    void setUpPERThreshold(double thresh)
    {
        mcsidx_up_per_threshold_ = thresh;
    }

    /** @brief Get PER threshold for decreasing modulation level */
    double getDownPERThreshold(void) const
    {
        return mcsidx_down_per_threshold_;
    }

    /** @brief Set PER threshold for decreasing modulation level */
    void setDownPERThreshold(double thresh)
    {
        mcsidx_down_per_threshold_ = thresh;
    }

    /** @brief Get MCS index learning alpha */
    double getMCSLearningAlpha(void) const
    {
        return mcsidx_alpha_;
    }

    /** @brief Set MCS index learning alpha */
    void setMCSLearningAlpha(double alpha)
    {
        mcsidx_alpha_ = alpha;
    }

    /** @brief Get MCS transition probability floor */
    double getMCSProbFloor(void) const
    {
        return mcsidx_prob_floor_;
    }

    /** @brief Set MCS transition probability floor */
    void setMCSProbFloor(double p)
    {
        mcsidx_prob_floor_ = p;
    }

    /** @brief Inform the controller that an environmental discontinuity has
     * occurred.
     * */
    void environmentDiscontinuity(void);

    /** @brief Get ACK delay. */
    double getACKDelay(void) const
    {
        return ack_delay_;
    }

    /** @brief Set ACK delay. */
    void setACKDelay(double t)
    {
        if (sack_delay_ >= ack_delay_)
            throw(std::out_of_range("SACK delays must be < ACK delay"));

        ack_delay_ = t;
    }

    /** @brief Get ACK delay estimation window (sec). */
    double getACKDelayEstimationWindow(void) const
    {
        return ack_delay_estimation_window_;
    }

    /** @brief Set ACK delay estimation window (sec). */
    void setACKDelayEstimationWindow(double t)
    {
        ack_delay_estimation_window_ = t;

        std::lock_guard<std::mutex> lock(send_mutex_);

        for (auto &&it : send_) {
            SendWindow                  &sendw = it.second;
            std::lock_guard<std::mutex> lock(sendw.mutex);

            sendw.ack_delay.setTimeWindow(t);
        }
    }

    /** @brief Get retransmission delay. */
    double getRetransmissionDelay(void) const
    {
        return retransmission_delay_;
    }

    /** @brief Set retransmission delay. */
    void setRetransmissionDelay(double t)
    {
        retransmission_delay_ = t;
    }

    /** @brief Get minimum retransmission delay. */
    double getMinRetransmissionDelay(void) const
    {
        return min_retransmission_delay_;
    }

    /** @brief Set minimum retransmission delay. */
    void setMinRetransmissionDelay(double t)
    {
        min_retransmission_delay_ = t;
    }

    /** @brief Get retransmission delay safety factor. */
    double getRetransmissionDelaySlop(void) const
    {
        return retransmission_delay_slop_;
    }

    /** @brief Set retransmission delay safety factor. */
    void setRetransmissionDelaySlop(double k)
    {
        retransmission_delay_slop_ = k;
    }

    /** @brief Get SACK delay. */
    double getSACKDelay(void) const
    {
        return sack_delay_;
    }

    /** @brief Set SACK delay. */
    void setSACKDelay(double t)
    {
        if (sack_delay_ >= ack_delay_)
            throw(std::out_of_range("SACK delays must be < ACK delay"));

        sack_delay_ = t;
    }

    /** @brief Get maximum number of SACKs in a packet. */
    std::optional<size_t> getMaxSACKs(void) const
    {
        return max_sacks_;
    }

    /** @brief Set maximum number of SACKs in a packet. */
    void setMaxSACKs(std::optional<size_t> max_sacks)
    {
        max_sacks_ = max_sacks;
    }

    /** @brief Return explicit NAK window size. */
    bool getExplicitNAKWindow(void) const
    {
        return explicit_nak_win_;
    }

    /** @brief Set explicit NAK window size. */
    void setExplicitNAKWindow(size_t n)
    {
        explicit_nak_win_ = n;
    }

    /** @brief Return explicit NAK window duration. */
    double getExplicitNAKWindowDuration(void) const
    {
        return explicit_nak_win_duration_;
    }

    /** @brief Set explicit NAK window duration. */
    void setExplicitNAKWindowDuration(double t)
    {
        explicit_nak_win_duration_ = t;
    }

    /** @brief Return whether or not we should send selective ACKs. */
    bool getSelectiveACK(void) const
    {
        return selective_ack_;
    }

    /** @brief Set whether or not we should send selective ACKs. */
    void setSelectiveACK(bool ack)
    {
        selective_ack_ = ack;
    }

    /** @brief Return selective ACK feedback delay. */
    double getSelectiveACKFeedbackDelay(void) const
    {
        return selective_ack_feedback_delay_;
    }

    /** @brief Set selective ACK feedback delay. */
    void setSelectiveACKFeedbackDelay(double delay)
    {
        selective_ack_feedback_delay_ = delay;
    }

    /** @brief Return maximum number of retransmission attempts. */
    std::optional<size_t> getMaxRetransmissions(void) const
    {
        return max_retransmissions_;
    }

    /** @brief Set maximum number of retransmission attempts. */
    void setMaxRetransmissions(std::optional<size_t> max_retransmissions)
    {
        max_retransmissions_ = max_retransmissions;
    }

    /** @brief Return flag indicating whether or not packets are always
     * demodulated in order.
     */
    bool getDemodAlwaysOrdered(void) const
    {
        return demod_always_ordered_;
    }

    /** @brief Set flag indicating whether or not packets are always
     * demodulated in order.
     */
    void setDemodAlwaysOrdered(bool always_ordered)
    {
        demod_always_ordered_ = always_ordered;
    }

    /** @brief Return flag indicating whether or not demodulation queue enforces
     * packet order.
     */
    bool getEnforceOrdering(void) const
    {
        return enforce_ordering_;
    }

    /** @brief Set whether or not demodulation queue enforces packet order. */
    void setEnforceOrdering(bool enforce)
    {
        enforce_ordering_ = enforce;
    }

    /** @brief Get maximum number of extra control bytes beyond MTU. */
    size_t getMCU(void) const
    {
        return mcu_;
    }

    /** @brief Set maximum number of extra control bytes beyond MTU. */
    void setMCU(size_t mcu)
    {
        mcu_ = mcu;
    }

    /** @brief Get whether or not we always move the send windwo along. */
    bool getMoveAlong(void) const
    {
        return move_along_;
    }

    /** @brief Set whether or not to always move the send windwo along. */
    void setMoveAlong(bool move_along)
    {
        move_along_ = move_along;
    }

    /** @brief Get whether or not we decrease the MCS index of retransmitted packets with a deadline. */
    bool getDecreaseRetransMCSIdx(void) const
    {
        return decrease_retrans_mcsidx_;
    }

    /** @brief Set whether or not we decrease the MCS index of retransmitted packets with a deadline. */
    void setDecreaseRetransMCSIdx(bool decrease_retrans_mcsidx)
    {
        decrease_retrans_mcsidx_ = decrease_retrans_mcsidx;
    }

    /** @brief Return true if timestamps exist for node, false otherwise */
    bool timestampsContains(NodeId node_id) const
    {
        std::lock_guard<std::mutex> lock(timestamps_mutex_);

        return timestamps_.find(node_id) != timestamps_.end();
    }

    /** @brief Return set of nodes with timestamps */
    std::set<NodeId> getTimestampsNodes(void) const
    {
        std::lock_guard<std::mutex> lock(timestamps_mutex_);
        std::set<NodeId>            nodes;

        for(auto const& it: timestamps_)
            nodes.insert(it.first);

        return nodes;
    }

    /** @brief Get timestamps */
    Timestamps::timestamps_map getTimestamps(NodeId node_id)
    {
        std::lock_guard<std::mutex> lock(timestamps_mutex_);

        return timestamps_[node_id].timestamps;
    }

    /** @brief Return true if send window exists for node, false otherwise */
    bool sendWindowContains(NodeId node_id) const
    {
        std::lock_guard<std::mutex> lock(send_mutex_);

        return send_.find(node_id) != send_.end();
    }

    /** @brief Return set of nodes with send windows */
    std::set<NodeId> getSendWindowNodes(void) const
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        std::set<NodeId>            nodes;

        for(auto const& it: send_)
            nodes.insert(it.first);

        return nodes;
    }

    /** @brief Return true if receive window exists for node, false otherwise */
    bool recvWindowContains(NodeId node_id) const
    {
        std::lock_guard<std::mutex> lock(recv_mutex_);

        return recv_.find(node_id) != recv_.end();
    }

    /** @brief Return set of nodes with receive windows */
    std::set<NodeId> getRecvWindowNodes(void) const
    {
        std::lock_guard<std::mutex> lock(recv_mutex_);
        std::set<NodeId>            nodes;

        for(auto const& it: recv_)
            nodes.insert(it.first);

        return nodes;
    }

    bool pull(std::shared_ptr<NetPacket> &pkt) override;

    void received(std::shared_ptr<RadioPacket> &&pkt) override;

    void transmitted(std::list<std::unique_ptr<ModPacket>> &mpkts) override;

    /** @brief Retransmit a send window entry on timeout. */
    void retransmitOnTimeout(SendWindow::Entry &entry);

    /** @brief Send an ACK to the given receiver. */
    /** The caller MUST hold the lock on recvw. */
    void ack(RecvWindow &recvw);

    /** @brief Send a NAK to the given receiver. */
    void nak(RecvWindow &recvw, Seq seq);

    /** @brief Broadcast a HELLO packet. */
    void broadcastHello(void);

    /** @brief Send a ping packet. */
    void sendPing(NodeId dest);

    /** @brief Send a pong packet. */
    void sendPong(NodeId dest);

    /** @brief Broadcast gain */
    Gain broadcast_gain;

    /** @brief ACK gain */
    Gain ack_gain;

protected:
    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Mutex to serialize access to the network */
    std::mutex net_mutex_;

    /** @brief Slot size (sec) */
    double slot_size_;

    /** @brief Latest environment discontinuity */
    std::optional<MonoClock::time_point> env_timestamp_;

    /** @brief MCS fast-adjustment period (sec) */
    double mcs_fast_adjustment_period_;

    /** @brief Maximum size of a send window */
    Seq::uint_type max_sendwin_;

    /** @brief Size of receive window */
    Seq::uint_type recvwin_;

    /** @brief Mutex for the send windows */
    mutable std::mutex send_mutex_;

    /** @brief Send windows */
    std::map<NodeId, SendWindow> send_;

    /** @brief Mutex for the receive windows */
    mutable std::mutex recv_mutex_;

    /** @brief Receive windows */
    std::map<NodeId, RecvWindow> recv_;

    /** @brief Mutex for timestamps */
    mutable std::mutex timestamps_mutex_;

    /** @brief Receive windows */
    std::map<NodeId, Timestamps> timestamps_;

    /** @brief Timer queue */
    TimerQueue timer_queue_;

    /** @brief Samples in modulated packet of max size at each MCS */
    std::vector<size_t> max_packet_samples_;

    /** @brief EVM thresholds */
    std::vector<evm_thresh_t> evm_thresholds_;

    /** @brief Time window used to calculate short-term PER */
    double short_per_window_;

    /** @brief Time window used to calculate long-term PER */
    double long_per_window_;

    /** @brief Time window used to calculate short-term statistics */
    double short_stats_window_;

    /** @brief Time window used to calculate long-term statistics */
    double long_stats_window_;

    /** @brief Broadcast MCS index */
    mcsidx_t mcsidx_broadcast_;

    /** @brief ACK MCS index */
    mcsidx_t mcsidx_ack_;

    /** @brief Minimum MCS index */
    mcsidx_t mcsidx_min_;

    /** @brief Maximum MCS index */
    mcsidx_t mcsidx_max_;

    /** @brief Initial MCS index */
    mcsidx_t mcsidx_init_;

    /** @brief PER threshold for increasing modulation level */
    double mcsidx_up_per_threshold_;

    /** @brief PER threshold for decreasing modulation level */
    double mcsidx_down_per_threshold_;

    /** @brief Multiplicative factor used when learning MCS transition
     * probabilities
     */
    double mcsidx_alpha_;

    /** @brief Minimum MCS transition probability */
    double mcsidx_prob_floor_;

    /** @brief ACK delay in seconds */
    double ack_delay_;

    /** @brief ACK delay estimation window (sec) */
    double ack_delay_estimation_window_;

    /** @brief Packet re-transmission delay in seconds */
    double retransmission_delay_;

    /** @brief Minimum packet re-transmission delay in seconds */
    double min_retransmission_delay_;

    /** @brief Safety factor for retransmission timer estimator */
    double retransmission_delay_slop_;

    /** @brief SACK delay (sec) */
    /** @brief Amount of time we wait for a regular packet to have a SACK
     * attached.
     */
    double sack_delay_;

    /** @brief Maximum number of SACKs in a packet */
    std::optional<size_t> max_sacks_;

    /** @brief Explicit NAK window */
    size_t explicit_nak_win_;

    /** @brief Explicit NAK window duration */
    double explicit_nak_win_duration_;

    /** @brief Should we send selective ACK packets? */
    bool selective_ack_;

    /** @brief Amount of time we wait to accept selective ACK feedback about a
     * packet
     */
    double selective_ack_feedback_delay_;

    /** @brief Maximum number of retransmission attempts
     */
    std::optional<size_t> max_retransmissions_;

    /** @brief Are packets always demodulated in order? */
    bool demod_always_ordered_;

    /** @brief Should packets always be output in the order they were actually
     * received?
     */
    bool enforce_ordering_;

    /** @brief Maximum extra control bytes, in contrast to MTU */
    size_t mcu_;

    /** @brief Always move the send window along, even if it's full */
    bool move_along_;

    /** @brief Decrease MCS index of retransmitted packets with a deadline */
    bool decrease_retrans_mcsidx_;

    /** @brief Current timestamp sequence number */
    std::atomic<TimestampSeq> timestamp_seq_;

    /** @brief Mutex for random number generator */
    std::mutex gen_mutex_;

    /** @brief Random number generator */
    std::mt19937 gen_;

    /** @brief Uniform 0-1 real distribution */
    std::uniform_real_distribution<double> dist_;

    /** @brief Re-transmit or drop a send window entry. */
    void retransmitOrDrop(SendWindow::Entry &entry);

    /** @brief Re-transmit a send window entry. */
    void retransmit(SendWindow::Entry &entry);

    /** @brief Drop a send window entry. */
    void drop(SendWindow::Entry &entry);

    /** @brief Advance the send window.
     * @param sendw The send window to advance
     */
    void advanceSendWindow(SendWindow &sendw);

    /** @brief Advance the receive window.
     * @param seq The sequence number received
     * @param recv The receive window to advance
     */
    void advanceRecvWindow(Seq seq, RecvWindow &recv);

    /** @brief Start the re-transmission timer if it is not set. */
    void startRetransmissionTimer(SendWindow::Entry &entry);

    /** @brief Start the selective ACK timer if it is not set. */
    void startSACKTimer(RecvWindow &recvw);

    /** @brief Handle HELLO and ping control messages. */
    void handleCtrlHelloAndPing(RadioPacket &pkt, Node &node);

    /** @brief Handle timestamp control messages. */
    void handleCtrlTimestamp(RadioPacket &pkt, Node &node);

    /** @brief Append control messages for feedback to sender. */
    /** This method appends feedback to the receiver in the form of both
     * statistics and selective ACKs .
     */
    void appendFeedback(const std::shared_ptr<NetPacket> &pkt, RecvWindow &recvw);

    /** @brief Handle receiver statistics. */
    void handleReceiverStats(RadioPacket &pkt, SendWindow &sendw);

    /** @brief Handle an ACK. */
    void handleACK(SendWindow &sendw, const Seq &seq);

    /** @brief Handle a NAK.
     * @param sendw The sender's window
     * @param pkt The packet potentially containing NAK's
     * @return The NAK with the highest sequence number, if there is a NAK
     */
    std::optional<Seq> handleNAK(RadioPacket &pkt, SendWindow &sendw);

    /** @brief Handle select ACK messages. */
    void handleSelectiveACK(const std::shared_ptr<RadioPacket> &pkt,
                            SendWindow &sendw,
                            MonoClock::time_point tfeedback);

    /** @brief Handle sender setting unack */
    void handleSetUnack(RadioPacket &pkt, RecvWindow &recvw);

    /** @brief Get a packet that is elligible to be sent. */
    bool getPacket(std::shared_ptr<NetPacket>& pkt);

    /** @brief Get a node's send window */
    SendWindow &getSendWindow(NodeId node_id);

    /** @brief Get a node's receive window
     * @param node_id The node whose window to get
     * @returns The receive window
     */
    RecvWindow &getRecvWindow(NodeId node_id);
};

class SendWindowGuard {
public:
    SendWindowGuard(SmartController &controller, NodeId node_id)
      : sendw_(controller.getSendWindow(node_id))
      , lock_(sendw_.mutex)
    {
    }

    SendWindowGuard() = delete;

    ~SendWindowGuard() = default;

    SendWindow &operator *()
    {
        return sendw_;
    }

    const SendWindow &operator *() const
    {
        return sendw_;
    }

    SendWindow *operator ->()
    {
        return &sendw_;
    }

    const SendWindow *operator ->() const
    {
        return &sendw_;
    }

private:
    SendWindow &sendw_;

    std::lock_guard<std::mutex> lock_;
};

class RecvWindowGuard {
public:
    RecvWindowGuard(SmartController &controller, NodeId node_id)
      : recvw_(controller.getRecvWindow(node_id))
      , lock_(recvw_.mutex)
    {
    }

    RecvWindowGuard() = delete;

    ~RecvWindowGuard() = default;

    RecvWindow &operator *()
    {
        return recvw_;
    }

    const RecvWindow &operator *() const
    {
        return recvw_;
    }

    RecvWindow *operator ->()
    {
        return &recvw_;
    }

    const RecvWindow *operator ->() const
    {
        return &recvw_;
    }

private:
    RecvWindow &recvw_;

    std::lock_guard<std::mutex> lock_;
};

#endif /* SMARTCONTROLLER_H_ */
