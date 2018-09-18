#ifndef SMARTCONTROLLER_H_
#define SMARTCONTROLLER_H_

#include <list>
#include <map>

#include "heap.hh"
#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "TimerQueue.hh"
#include "TimeSync.hh"
#include "net/Queue.hh"
#include "mac/Controller.hh"
#include "mac/MAC.hh"

class SmartController;

struct SendWindow : public TimerQueue::Timer {
    using vector_type = std::vector<std::shared_ptr<NetPacket>>;

    SendWindow(NodeId node_id, SmartController &controller, Seq::uint_type maxwin)
      : node_id(node_id)
      , controller(controller)
      , unack(0)
      , max(0)
      , new_window(true)
      , win(1)
      , maxwin(maxwin)
      , mcsidx(0)
      , pkts(maxwin)
    {}

    /** @brief Node ID of destination. */
    NodeId node_id;

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

    /** @brief First sequence number at this modulation index */
    Seq mcsidx_init_seq;

    /** @brief Pending packets we can't send because our window isn't large enough */
    std::list<std::shared_ptr<NetPacket>> pending;

    /** @brief Mutex for the send window */
    spinlock_mutex mutex;

    /** @brief Return the packet with the given sequence number in the window */
    std::shared_ptr<NetPacket>& operator[](Seq seq)
    {
        return pkts[seq % pkts.size()];
    }

    void operator()() override;

private:
    /** @brief Unacknowledged packets in our send window. */
    /** INVARIANT: unack <= N <= max < unack + win */
    vector_type pkts;
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

    RecvWindow(NodeId node_id, SmartController &controller, Seq::uint_type win)
      : node_id(node_id)
      , controller(controller)
      , ack(0)
      , max(0)
      , win(win)
      , entries(win)
    {}

    /** @brief Node ID of destination. */
    NodeId node_id;

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
    * our receive, window, which should therefore be empty, and we should ACK
    * ack+1. Otherwise we have a hole in our receive window and we should NAK
    * ack+1. Note than a NAK of sequence number N+1 implicitly ACKs N, since
    * otherwise we would've NAK'ed N instead.
    */
    Seq max;

    /** @brief Timestamp of packet with the maximum sequence number we have
      * sent. */
    Clock::time_point max_timestamp;

    /** @brief Receive window size */
    Seq::uint_type win;

    /** @brief Mutex for the receive window */
    spinlock_mutex mutex;

    /** @brief Return the packet with the given sequence number in the window */
    Entry& operator[](Seq seq)
    {
        return entries[seq % entries.size()];
    }

    void operator()() override;

private:
    /** @brief All packets with sequence numbers N such that
     * ack <= N <= max < ack + win
     */
    vector_type entries;
};

/** @brief A MAC controller that implements ARQ. */
class SmartController : public Controller
{
public:
    SmartController(std::shared_ptr<Net> net,
                    Seq::uint_type max_sendwin,
                    Seq::uint_type recvwin,
                    double mcsidx_up_per_threshold,
                    double mcsidx_down_per_threshold);
    virtual ~SmartController();

    bool pull(std::shared_ptr<NetPacket>& pkt) override;

    void received(std::shared_ptr<RadioPacket>&& pkt) override;

    void retransmit(SendWindow &sendw);

    /** @brief Send an ACK to the given receiver. The caller MUST hold the lock
     * on recvw.
     */
    void ack(RecvWindow &recvw);

    /** @brief Send a NAK to the given receiver. The caller MUST hold the lock
     * on recvw.
     */
    void nak(NodeId node_id, Seq seq);

    void broadcastHello(void);

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
        return mac_;
    }

    /** @brief Set the controller's MAC. */
    void setMAC(std::shared_ptr<MAC> mac)
    {
        mac_ = mac;
    }

    /** @brief Get number of samples in a trnsmission slot */
    size_t getSlotSize(void)
    {
        return slot_size_;
    }

    /** @brief Set number of samples in a trnsmission slot */
    void setSlotSize(size_t size)
    {
        slot_size_ = size;
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

    /** @brief Broadcast TX params */
    TXParams broadcast_tx_params;

protected:
    /** @brief Our MAC. */
    std::shared_ptr<MAC> mac_;

    /** @brief Network queue with high-priority sub-queue. */
    std::shared_ptr<NetQueue> netq_;

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
    size_t slot_size_;

    /** @brief PER threshold for increasing modulation level */
    double mcsidx_up_per_threshold_;

    /** @brief PER threshold for decreasing modulation level */
    double mcsidx_down_per_threshold_;

    /** @brief Should packets always be output in the order they were actually
     * received?
     */
    bool enforce_ordering_;

    /** @brief Time sync information */
    struct TimeSync time_sync_;

    /** @brief Start the re-transmission timer if it is not set. */
    void startRetransmissionTimer(SendWindow &sendw);

    /** @brief Start the ACK timer if it is not set. */
    void startACKTimer(RecvWindow &recvw);

    /** @brief Handle HELLO and timestamp control messages. */
    void handleCtrlHello(Node &node, std::shared_ptr<RadioPacket>& pkt);

    /** @brief Handle timestamp delta control messages. */
    void handleCtrlTimestampDeltas(Node &node, std::shared_ptr<RadioPacket>& pkt);

    /** @brief Handle NAK control messages. */
    void handleCtrlNAK(Node &node, std::shared_ptr<RadioPacket>& pkt);

    /** @brief Append NAK control messages. */
    void appendCtrlNAK(RecvWindow &recvw, std::shared_ptr<NetPacket>& pkt);

    /** @brief Handle a NAK. */
    void handleNak(SendWindow &sendw, Node &dest, const Seq &seq, bool explicitNak);

    /** @brief Handle a successful packet transmission. */
    void txSuccess(SendWindow &sendw, Node &node);

    /** @brief Handle an unsuccessful packet transmission. */
    void txFailure(SendWindow &sendw, Node &node);

    /** @brief Get a packet that is elligible to be sent. */
    bool getPacket(std::shared_ptr<NetPacket>& pkt);

    /** @brief Reconfigure a node's PER estimates */
    void resetPEREstimates(Node &node);

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
