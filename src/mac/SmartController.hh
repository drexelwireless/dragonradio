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
#include "RadioConfig.hh"
#include "TimerQueue.hh"
#include "net/Queue.hh"
#include "mac/Controller.hh"
#include "mac/MAC.hh"
#include "phy/Gain.hh"
#include "phy/PHY.hh"

class SmartController;

struct SendWindow {
    struct Entry : public TimerQueue::Timer {
        Entry(SendWindow &sendw)
          : sendw(sendw)
          , pkt(nullptr)
          , timestamp(0.0)
          , mcsidx(0)
          , nretrans(0)
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
            return !pkt->isFlagSet(kSYN);
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
            return !pkt->isFlagSet(kSYN) &&
                 (  (max_retransmissions && nretrans >= *max_retransmissions)
                 || pkt->deadlinePassed(MonoClock::now()));
        }

        void operator()() override;

        /** @brief The send window. */
        SendWindow &sendw;

        /** @brief The packet received in this window entry. */
        std::shared_ptr<NetPacket> pkt;

        /** @brief Timestamp of last transmission of this packet. */
        Clock::time_point timestamp;

        /** @brief Modulation index used for last transmission of this packet */
        size_t mcsidx;

        /** @brief Number of retransmissions for this packet. */
        /** The retransmission count will be 0 on the first transmission. */
        size_t nretrans;
    };

    using vector_type = std::vector<Entry>;

    SendWindow(Node &n, SmartController &controller, Seq::uint_type maxwin)
      : node(n)
      , controller(controller)
      , unack(0)
      , max(0)
      , new_window(true)
      , win(1)
      , maxwin(maxwin)
      , mcsidx(0)
      , mcsidx_prob(0)
      , entries_(maxwin, *this)
    {
    }

    /** @brief Destination node. */
    Node &node;

    /** @brief Our controller. */
    SmartController &controller;

    /** @brief First un-ACKed sequence number. */
    std::atomic<Seq> unack;

    /** @brief Maximum sequence number we have sent. */
    /** INVARIANT: max < unack + win */
    std::atomic<Seq> max;

    /** @brief Is this a new window? */
    bool new_window;

    /** @brief Send window size */
    Seq::uint_type win;

    /** @brief Maximum window size */
    Seq::uint_type maxwin;

    /** @brief Modulation index */
    size_t mcsidx;

    /** @brief The probability of moving to a given MCS */
    std::vector<double> mcsidx_prob;

    /** @brief End of the current PER window PER. */
    /** Every packet up to, but not including, this sequence number has already been
     * used to calculate the current PER
     */
    Seq per_end;

    /** @brief Mutex for the send window */
    spinlock_mutex mutex;

    /** @brief Return the packet with the given sequence number in the window */
    Entry& operator[](Seq seq)
    {
        return entries_[seq % entries_.size()];
    }

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
      , entries_(win)
    {}

    /** @brief Sender node. */
    Node &node;

    /** @brief Our controller. */
    SmartController &controller;

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

    /** @brief Mutex for the receive window */
    spinlock_mutex mutex;

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

/** @brief A MAC controller that implements ARQ. */
class SmartController : public Controller
{
    friend class SendWindow;
    friend class RecvWindow;

public:
    SmartController(std::shared_ptr<Net> net,
                    std::shared_ptr<PHY> phy,
                    double slot_size,
                    Seq::uint_type max_sendwin,
                    Seq::uint_type recvwin,
                    unsigned mcsidx_init,
                    double mcsidx_up_per_threshold,
                    double mcsidx_down_per_threshold,
                    double mcsidx_alpha,
                    double mcsidx_prob_floor);
    virtual ~SmartController();

    /** @brief Get the controller's network queue. */
    std::shared_ptr<NetQueue> getNetQueue(void)
    {
        return netq_;
    }

    /** @brief Set the controller's network queue. */
    void setNetQueue(std::shared_ptr<NetQueue> q)
    {
        netq_ = q;
    }

    /** @brief Get the controller's MAC. */
    std::shared_ptr<MAC> getMAC(void)
    {
        std::lock_guard<std::mutex> lock(mac_mutex_);

        return mac_;
    }

    /** @brief Set the controller's MAC. */
    void setMAC(std::shared_ptr<MAC> mac)
    {
        std::lock_guard<std::mutex> lock(mac_mutex_);

        mac_ = mac;
    }

    /** @brief Get number of samples in a transmission slot */
    size_t getSamplesPerSlot(void) const
    {
        return samples_per_slot_;
    }

    /** @brief Set number of samples in a transmission slot */
    void setSamplesPerSlot(size_t samples_per_slot)
    {
        samples_per_slot_ = samples_per_slot;
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

    /** @brief Get echoed timestamps */
    timestamp_vector getEchoedTimestamps(void)
    {
        std::lock_guard<std::mutex> lock(echoed_timestamps_mutex_);

        return echoed_timestamps_;
    }

    /** @brief Return maximum number of packets per slot.
     * @param p The TXParams uses for modulation
     * @returns The number of packets of maximum size that can fit in one slot
     *          with the given modulation scheme.
     */
    size_t getMaxPacketsPerSlot(const TXParams &p);

    bool pull(std::shared_ptr<NetPacket>& pkt) override;

    void received(std::shared_ptr<RadioPacket>&& pkt) override;

    void missed(std::shared_ptr<NetPacket>&& pkt) override;

    void transmitted(std::shared_ptr<NetPacket>& pkt) override;

    /** @brief Retransmit a send window entry on timeout. */
    void retransmitOnTimeout(SendWindow::Entry &entry);

    /** @brief Send an ACK to the given receiver. */
    /** The caller MUST hold the lock on recvw. */
    void ack(RecvWindow &recvw);

    /** @brief Send a NAK to the given receiver. */
    void nak(NodeId node_id, Seq seq);

    /** @brief Broadcast a HELLO packet. */
    void broadcastHello(void);

    /** @brief Broadcast TX params */
    TXParams broadcast_tx_params;

    /** @brief Broadcast gain */
    Gain broadcast_gain;

    /** @brief ACK gain */
    Gain ack_gain;

protected:
    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Mutex protecting the MAC. */
    std::mutex mac_mutex_;

    /** @brief Our MAC. */
    std::shared_ptr<MAC> mac_;

    /** @brief Network queue with high-priority sub-queue. */
    std::shared_ptr<NetQueue> netq_;

    /** @brief Slot size (sec) */
    double slot_size_;

    /** @brief Maximum size of a send window */
    Seq::uint_type max_sendwin_;

    /** @brief Size of receive window */
    Seq::uint_type recvwin_;

    /** @brief Send windows */
    std::map<NodeId, SendWindow> send_;

    /** @brief Mutex for the send windows */
    spinlock_mutex send_mutex_;

    /** @brief Receive windows */
    std::map<NodeId, RecvWindow> recv_;

    /** @brief Mutex for the receive windows */
    spinlock_mutex recv_mutex_;

    /** @brief Timer queue */
    TimerQueue timer_queue_;

    /** @brief Number of samples in a transmission slot */
    size_t samples_per_slot_;

    /** @brief Initial MCS index */
    unsigned mcsidx_init_;

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

    /** @brief Should packets always be output in the order they were actually
     * received?
     */
    bool enforce_ordering_;

    /** @brief Maximum extra control bytes, in contrast to MTU */
    size_t mcu_;

    /** @brief Always move the send window along, even if it's full */
    bool move_along_;

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
     * @param unack The new value of the send window's unack parameter
     */
    void advanceSendWindow(SendWindow &sendw, Seq unack, Seq max);

    /** @brief Start the re-transmission timer if it is not set. */
    void startRetransmissionTimer(SendWindow::Entry &entry);

    /** @brief Start the selective ACK timer if it is not set. */
    void startSACKTimer(RecvWindow &recvw);

    /** @brief Handle HELLO and timestamp control messages. */
    void handleCtrlHello(Node &node, std::shared_ptr<RadioPacket>& pkt);

    /** @brief Handle timestampecho control messages. */
    void handleCtrlTimestampEchos(Node &node, std::shared_ptr<RadioPacket>& pkt);

    /** @brief Append ACK control messages. */
    void appendCtrlACK(RecvWindow &recvw, std::shared_ptr<NetPacket>& pkt);

    /** @brief Handle an ACK. */
    void handleACK(SendWindow &sendw, const Seq &seq);

    /** @brief Handle a NAK.
     * @param sendw The sender's window
     * @param pkt The packet potentially containing NAK's
     * @return The NAK with the highest sequence number, if there is a NAK
     */
    std::optional<Seq> handleNAK(SendWindow &sendw,
                                 std::shared_ptr<RadioPacket>& pkt);

    /** @brief Handle select ACK messages. */
    void handleSelectiveACK(SendWindow &sendw,
                            std::shared_ptr<RadioPacket>& pkt,
                            Clock::time_point tfeedback);

    /** @brief Update PER as a result of successful packet transmission. */
    void txSuccess(Node &node);

    /** @brief Update PER as a result of unsuccessful packet transmission. */
    void txFailure(Node &node);

    /** @brief Update MCS based on current PER */
    void updateMCS(SendWindow &sendw);

    /** @brief Move down one MCS level */
    void moveDownMCS(SendWindow &sendw);

    /** @brief Move up one MCS level */
    void moveUpMCS(SendWindow &sendw);

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
    SendWindow &getSendWindow(NodeId node_id);

    /** @brief Get a node's receive window.
     * @param node_id The node whose window to get
     * @returns A pointer to the window or nullptr if one doesn't exist.
     */
    RecvWindow *maybeGetReceiveWindow(NodeId node_id);

    /** @brief Get a node's receive window */
    RecvWindow &getReceiveWindow(NodeId node_id, Seq seq, bool isSYN);
};

#endif /* SMARTCONTROLLER_H_ */
