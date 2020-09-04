#ifndef SMARTCONTROLLER_H_
#define SMARTCONTROLLER_H_

#include <sys/types.h>
#include <netinet/if_ether.h>

#include <list>
#include <map>
#include <random>

#include "heap.hh"
#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "TimerQueue.hh"
#include "mac/Controller.hh"
#include "mac/MAC.hh"
#include "phy/Gain.hh"
#include "phy/PHY.hh"
#include "stats/Estimator.hh"
#include "stats/TimeWindowEstimator.hh"

class SmartController;

struct SendWindow {
    struct Entry : public TimerQueue::Timer {
        Entry(SendWindow &sendw)
          : sendw(sendw)
          , pkt(nullptr)
          , timestamp(0.0)
         {
         };

        virtual ~Entry() = default;

        void operator =(std::shared_ptr<NetPacket>& p)
        {
            pkt = p;
        }

        operator bool()
        {
            return (bool) pkt;
        }

        operator std::shared_ptr<NetPacket>()
        {
            return pkt;
        }

        void reset(void)
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
        MonoClock::time_point timestamp;
    };

    using vector_type = std::vector<Entry>;

    SendWindow(Node &n,
               SmartController &controller,
               Seq::uint_type maxwin,
               double retransmission_delay_)
      : node(n)
      , controller(controller)
      , seq({0})
      , unack({0})
      , max({0})
      , new_window(true)
      , send_set_unack(false)
      , win(1)
      , maxwin(maxwin)
      , mcsidx(0)
      , mcsidx_prob(0)
      , per_cutoff({0})
      , prev_short_per(1)
      , prev_long_per(1)
      , short_per(1)
      , long_per(1)
      , retransmission_delay(retransmission_delay_)
      , ack_delay(1.0)
      , entries_(maxwin, *this)
    {
    }

    /** @brief Destination node. */
    Node &node;

    /** @brief Our controller. */
    SmartController &controller;

    /** @brief Mutex for the send window */
    spinlock_mutex mutex;

    /** @brief Current sequence number for this destination */
    Seq seq;

    /** @brief First un-ACKed sequence number. */
    Seq unack;

    /** @brief Maximum sequence number we have sent. */
    /** INVARIANT: max < unack + win */
    Seq max;

    /** @brief Is this a new window? */
    bool new_window;

    /** @brief Do we need to send a set unack control message? */
    bool send_set_unack;

    /** @brief Send window size */
    Seq::uint_type win;

    /** @brief Maximum window size */
    Seq::uint_type maxwin;

    /** @brief Modulation index */
    size_t mcsidx;

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

    /** @brief Long-term EVM, as reported by receiver */
    std::optional<double> long_evm;

    /** @brief Long-term RSSI, as reported by receiver */
    std::optional<double> long_rssi;

    /** @brief Duration of retransmission timer */
    double retransmission_delay;

    /** @brief ACK delay estimator */
    TimeWindowMax<MonoClock, double> ack_delay;

    /** @brief Return the packet with the given sequence number in the window */
    Entry& operator[](Seq seq)
    {
        return entries_[seq % entries_.size()];
    }

    /** @brief Record a packet ACK */
    void recordACK(const MonoClock::time_point &tx_time);

private:
    /** @brief Unacknowledged packets in our send window. */
    /** INVARIANT: unack <= N <= max < unack + win */
    vector_type entries_;
};

struct RecvWindow : public TimerQueue::Timer  {
    struct Entry {
        Entry() : received(false), delivered(false), pkt(nullptr) {};

        void operator =(std::shared_ptr<RadioPacket>&& p)
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

    using vector_type = std::vector<Entry>;

    RecvWindow(Node &n,
               SmartController &controller,
               Seq seq,
               Seq::uint_type win,
               size_t nak_win)
      : node(n)
      , controller(controller)
      , ack(seq)
      , max(seq-1)
      , win(win)
      , need_selective_ack(false)
      , timer_for_ack(false)
      , explicit_nak_win(nak_win)
      , explicit_nak_idx(0)
      , long_evm(0)
      , long_rssi(0)
      , entries_(win)
    {}

    /** @brief Sender node. */
    Node &node;

    /** @brief Our controller. */
    SmartController &controller;

    /** @brief Mutex for the receive window */
    spinlock_mutex mutex;

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

    /** @brief Long-term packet error rate */
    TimeWindowMean<MonoClock, double> long_evm;

    /** @brief Long-term packet RSSI */
    TimeWindowMean<MonoClock, double> long_rssi;

    /** @brief Return the packet with the given sequence number in the window */
    Entry& operator[](Seq seq)
    {
#if defined(DEBUG)
        assert(seq >= ack && seq <= ack + win && max <= ack + win);
#endif /* defined(DEBUG) */
        return entries_[seq % entries_.size()];
    }

    void operator()() override;

private:
    /** @brief All packets with sequence numbers N such that
     * ack <= N <= max < ack + win
     */
    vector_type entries_;
};

class SendWindowsProxy;
class SendWindowProxy;

/** @brief A MAC controller that implements ARQ. */
class SmartController : public Controller
{
    friend class SendWindow;
    friend class RecvWindow;

    friend class SendWindowsProxy;
    friend class SendWindowProxy;

    friend class ReceiveWindowsProxy;
    friend class ReceiveWindowProxy;

public:
    using evm_thresh_t = std::optional<double>;

    SmartController(std::shared_ptr<Net> net,
                    std::shared_ptr<PHY> phy,
                    double slot_size,
                    Seq::uint_type max_sendwin,
                    Seq::uint_type recvwin,
                    const std::vector<evm_thresh_t> &evm_thresholds);
    virtual ~SmartController();

    /** @brief Get short time window over which to calculate PER (sec) */
    double getShortPERWindow(void)
    {
        return short_per_window_;
    }

    /** @brief Set short time window over which to calculate PER (sec) */
    void setShortPERWindow(double window)
    {
        short_per_window_ = window;
    }

    /** @brief Get long time window over which to calculate PER (sec) */
    double getLongPERWindow(void)
    {
        return long_per_window_;
    }

    /** @brief Set long time window over which to calculate PER (sec) */
    void setLongPERWindow(double window)
    {
        long_per_window_ = window;
    }

    /** @brief Get time window for statistics collection (sec) */
    double getLongStatsWindow(void)
    {
        return long_stats_window_;
    }

    /** @brief Set time window for statistics collection (sec) */
    void setLongStatsWindow(double window)
    {
        long_stats_window_ = window;
    }

    /** @brief Get broadcast MCS index */
    mcsidx_t getBroadcastMCSIndex(void)
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
    mcsidx_t getAckMCSIndex(void)
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
    mcsidx_t getMinMCSIndex(void)
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
    mcsidx_t getMaxMCSIndex(void)
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
    mcsidx_t getInitialMCSIndex(void)
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
    double getUpPERThreshold(void)
    {
        return mcsidx_up_per_threshold_;
    }

    /** @brief Set PER threshold for increasing modulation level */
    void setUpPERThreshold(double thresh)
    {
        mcsidx_up_per_threshold_ = thresh;
    }

    /** @brief Get PER threshold for decreasing modulation level */
    double getDownPERThreshold(void)
    {
        return mcsidx_down_per_threshold_;
    }

    /** @brief Set PER threshold for decreasing modulation level */
    void setDownPERThreshold(double thresh)
    {
        mcsidx_down_per_threshold_ = thresh;
    }

    /** @brief Get MCS index learning alpha */
    double getMCSLearningAlpha(void)
    {
        return mcsidx_alpha_;
    }

    /** @brief Set MCS index learning alpha */
    void setMCSLearningAlpha(double alpha)
    {
        mcsidx_alpha_ = alpha;
    }

    /** @brief Get MCS transition probability floor */
    double getMCSProbFloor(void)
    {
        return mcsidx_prob_floor_;
    }

    /** @brief Set MCS transition probability floor */
    void setMCSProbFloor(double p)
    {
        mcsidx_prob_floor_ = p;
    }

    /** @brief Reset all MCS transition probabilities to 1.0 */
    void resetMCSTransitionProbabilities(void);

    /** @brief Get ACK delay. */
    double getACKDelay(void)
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

        std::lock_guard<spinlock_mutex> lock(send_mutex_);

        for (auto &&it : send_) {
            SendWindow                      &sendw = it.second;
            std::lock_guard<spinlock_mutex> lock(sendw.mutex);

            sendw.ack_delay.setTimeWindow(t);
        }
    }

    /** @brief Get retransmission delay. */
    double getRetransmissionDelay(void)
    {
        return retransmission_delay_;
    }

    /** @brief Set retransmission delay. */
    void setRetransmissionDelay(double t)
    {
        retransmission_delay_ = t;
    }

    /** @brief Get minimum retransmission delay. */
    double getMinRetransmissionDelay(void)
    {
        return min_retransmission_delay_;
    }

    /** @brief Set minimum retransmission delay. */
    void setMinRetransmissionDelay(double t)
    {
        min_retransmission_delay_ = t;
    }

    /** @brief Get retransmission delay safety factor. */
    double getRetransmissionDelaySlop(void)
    {
        return retransmission_delay_slop_;
    }

    /** @brief Set retransmission delay safety factor. */
    void setRetransmissionDelaySlop(double k)
    {
        retransmission_delay_slop_ = k;
    }

    /** @brief Get SACK delay. */
    double getSACKDelay(void)
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

    /** @brief Return explicit NAK window size. */
    bool getExplicitNAKWindow(void)
    {
        return explicit_nak_win_;
    }

    /** @brief Set explicit NAK window size. */
    void setExplicitNAKWindow(size_t n)
    {
        explicit_nak_win_ = n;
    }

    /** @brief Return explicit NAK window duration. */
    double getExplicitNAKWindowDuration(void)
    {
        return explicit_nak_win_duration_;
    }

    /** @brief Set explicit NAK window duration. */
    void setExplicitNAKWindowDuration(double t)
    {
        explicit_nak_win_duration_ = t;
    }

    /** @brief Return whether or not we should send selective ACKs. */
    bool getSelectiveACK(void)
    {
        return selective_ack_;
    }

    /** @brief Set whether or not we should send selective ACKs. */
    void setSelectiveACK(bool ack)
    {
        selective_ack_ = ack;
    }

    /** @brief Return selective ACK feedback delay. */
    double getSelectiveACKFeedbackDelay(void)
    {
        return selective_ack_feedback_delay_;
    }

    /** @brief Set selective ACK feedback delay. */
    void setSelectiveACKFeedbackDelay(double delay)
    {
        selective_ack_feedback_delay_ = delay;
    }

    /** @brief Return maximum number of retransmission attempts. */
    std::optional<size_t> getMaxRetransmissions(void)
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
    bool getDemodAlwaysOrdered(void)
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
    bool getEnforceOrdering(void)
    {
        return enforce_ordering_;
    }

    /** @brief Set whether or not demodulation queue enforces packet order. */
    void setEnforceOrdering(bool enforce)
    {
        enforce_ordering_ = enforce;
    }

    /** @brief Get maximum number of extra control bytes beyond MTU. */
    size_t getMCU(void)
    {
        return mcu_;
    }

    /** @brief Set maximum number of extra control bytes beyond MTU. */
    void setMCU(size_t mcu)
    {
        mcu_ = mcu;
    }

    /** @brief Get whether or not we always move the send windwo along. */
    bool getMoveAlong(void)
    {
        return move_along_;
    }

    /** @brief Set whether or not to always move the send windwo along. */
    void setMoveAlong(bool move_along)
    {
        move_along_ = move_along;
    }

    /** @brief Get whether or not we decrease the MCS index of retransmitted packets with a deadline. */
    bool getDecreaseRetransMCSIdx(void)
    {
        return decrease_retrans_mcsidx_;
    }

    /** @brief Set whether or not we decrease the MCS index of retransmitted packets with a deadline. */
    void setDecreaseRetransMCSIdx(bool decrease_retrans_mcsidx)
    {
        decrease_retrans_mcsidx_ = decrease_retrans_mcsidx;
    }

    /** @brief Get echoed timestamps */
    timestamp_vector getEchoedTimestamps(void)
    {
        std::lock_guard<std::mutex> lock(echoed_timestamps_mutex_);

        return echoed_timestamps_;
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

    /** @brief Maximum size of a send window */
    Seq::uint_type max_sendwin_;

    /** @brief Size of receive window */
    Seq::uint_type recvwin_;

    /** @brief Mutex for the send windows */
    spinlock_mutex send_mutex_;

    /** @brief Send windows */
    std::map<NodeId, SendWindow> send_;

    /** @brief Mutex for the receive windows */
    spinlock_mutex recv_mutex_;

    /** @brief Receive windows */
    std::map<NodeId, RecvWindow> recv_;

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

    /** @brief Mutex for timestamps */
    std::mutex echoed_timestamps_mutex_;

    /** @brief Our timestamps as received by time master */
    timestamp_vector echoed_timestamps_;

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

    /** @brief Handle HELLO and timestamp control messages. */
    void handleCtrlHello(RadioPacket &pkt, Node &node);

    /** @brief Handle timestampecho control messages. */
    void handleCtrlTimestampEchos(RadioPacket &pkt, Node &node);

    /** @brief Append control messages for feedback to sender. */
    /** This method appends feedback to the receiver in the form of both
     * statistics and selective ACKs .
     */
    void appendFeedback(NetPacket &pkt, RecvWindow &recvw);

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
    void handleSelectiveACK(RadioPacket &pkt,
                            SendWindow &sendw,
                            MonoClock::time_point tfeedback);

    /** @brief Handle sender setting unack */
    void handleSetUnack(RadioPacket &pkt, RecvWindow &recvw);

    /** @brief Update PER as a result of successful packet transmission. */
    void txSuccess(SendWindow &sendw);

    /** @brief Update PER as a result of unsuccessful packet transmission. */
    void txFailure(SendWindow &sendw);

    /** @brief Update MCS based on current PER */
    void updateMCS(SendWindow &sendw);

    /** @brief Return true if we may move up one MCS level */
    bool mayMoveUpMCS(const SendWindow &sendw);

    /** @brief Move down one MCS level */
    void moveDownMCS(SendWindow &sendw, unsigned n);

    /** @brief Move up one MCS level */
    void moveUpMCS(SendWindow &sendw);

    /** @brief Set MCS */
    void setMCS(SendWindow &sendw, size_t mcsidx);

    /** @brief Reconfigure a node's PER estimates */
    void resetPEREstimates(SendWindow &sendw);

    /** @brief Get a packet that is elligible to be sent. */
    bool getPacket(std::shared_ptr<NetPacket>& pkt);

    /** @brief Get a node's send window.
     * @param node_id The node whose window to get
     * @returns A pointer to the window or nullptr if one doesn't exist.
     */
    SendWindow *maybeGetSendWindow(NodeId node_id);

    /** @brief Get a node's send window */
    SendWindow &getSendWindow(Node &node);

    /** @brief Get a node's send window */
    SendWindow &getSendWindow(NodeId node_id);

    /** @brief Get a node's receive window.
     * @param node_id The node whose window to get
     * @returns A pointer to the window or nullptr if one doesn't exist.
     */
    RecvWindow *maybeGetReceiveWindow(NodeId node_id);

    /** @brief Get a node's receive window */
    RecvWindow &getReceiveWindow(NodeId node_id, Seq seq, bool isSYN);
};

/** @brief A proxy object for a SmartController send window */
class SendWindowProxy
{
public:
    SendWindowProxy(std::shared_ptr<SmartController> controller,
                    NodeId node_id)
      : controller_(controller)
      , node_id_(node_id)
    {
        if (controller_->maybeGetSendWindow(node_id_) == nullptr)
            throw std::out_of_range("No send window for node");
    }

    double getShortPER(void)
    {
        SendWindow                      &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        return sendw.short_per.getValue();
    }

    double getLongPER(void)
    {
        SendWindow                      &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        return sendw.long_per.getValue();
    }

    std::optional<double> getLongEVM(void)
    {
        SendWindow                      &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        if (sendw.long_evm)
            return *sendw.long_evm;
        else
            return std::nullopt;
    }

    std::optional<double> getLongRSSI(void)
    {
        SendWindow                      &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        if (sendw.long_rssi)
            return *sendw.long_rssi;
        else
            return std::nullopt;
    }

private:
    /** @brief This send window's SmartController */
    std::shared_ptr<SmartController> controller_;

    /** @brief This send window's node ID */
    const NodeId node_id_;
};

/** @brief A proxy object for SmartController's send windows */
class SendWindowsProxy
{
public:
    SendWindowsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    SendWindowsProxy() = delete;
    ~SendWindowsProxy() = default;

    SendWindowProxy operator [](NodeId node)
    {
        return SendWindowProxy(controller_, node);
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

/** @brief A proxy object for a SmartController receive window */
class ReceiveWindowProxy
{
public:
    ReceiveWindowProxy(std::shared_ptr<SmartController> controller,
                       NodeId node_id)
      : controller_(controller)
      , node_id_(node_id)
    {
        if (controller_->maybeGetReceiveWindow(node_id_) == nullptr)
            throw std::out_of_range("No receive window for node");
    }

    double getLongEVM(void)
    {
        RecvWindow                      &recvw = *controller_->maybeGetReceiveWindow(node_id_);
        std::lock_guard<spinlock_mutex> lock(recvw.mutex);

        return recvw.long_evm.getValue();
    }

    double getLongRSSI(void)
    {
        RecvWindow                      &recvw = *controller_->maybeGetReceiveWindow(node_id_);
        std::lock_guard<spinlock_mutex> lock(recvw.mutex);

        return recvw.long_rssi.getValue();
    }

private:
    /** @brief This send window's SmartController */
    std::shared_ptr<SmartController> controller_;

    /** @brief This send window's node ID */
    const NodeId node_id_;
};

/** @brief A proxy object for SmartController's receive windows */
class ReceiveWindowsProxy
{
public:
    ReceiveWindowsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    ReceiveWindowsProxy() = delete;
    ~ReceiveWindowsProxy() = default;

    ReceiveWindowProxy operator [](NodeId node)
    {
        return ReceiveWindowProxy(controller_, node);
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

#endif /* SMARTCONTROLLER_H_ */
