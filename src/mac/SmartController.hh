#ifndef SMARTCONTROLLER_H_
#define SMARTCONTROLLER_H_

#include <list>
#include <map>

#include "heap.hh"
#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "TimerQueue.hh"
#include "net/SpliceQueue.hh"
#include "mac/Controller.hh"

class SmartController;

struct SendWindow : public TimerQueue::Timer {
    using vector_type = std::vector<std::shared_ptr<NetPacket>>;

    SendWindow(NodeId node_id, SmartController &controller, Seq::uint_type maxwin)
      : node_id(node_id)
      , controller(controller)
      , unack(0)
      , max(0)
      , win(1)
      , maxwin(maxwin)
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

    /** @brief Send window size */
    Seq::uint_type win;

    /** @brief Maximumend window size */
    Seq::uint_type maxwin;

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
    using vector_type = std::vector<std::shared_ptr<RadioPacket>>;

    RecvWindow(NodeId node_id, SmartController &controller, Seq::uint_type win)
      : node_id(node_id)
      , controller(controller)
      , ack(0)
      , max(0)
      , win(win)
      , pkts(win)
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

    /** @brief Receive window size */
    Seq::uint_type win;

    /** @brief Mutex for the receive window */
    spinlock_mutex mutex;

    /** @brief Return the packet with the given sequence number in the window */
    std::shared_ptr<RadioPacket>& operator[](Seq seq)
    {
        assert(pkts[seq % pkts.size()].use_count() >= 0);
        return pkts[seq % pkts.size()];
    }

    void operator()() override;

private:
    /** @brief All packets with sequence numbers N such that
     * ack <= N <= max < ack + win
     */
    vector_type pkts;
};

/** @brief A MAC controller that implements ARQ. */
class SmartController : public Controller
{
public:
    SmartController(std::shared_ptr<Net> net,
                    Seq::uint_type max_sendwin,
                    Seq::uint_type recvwin);
    virtual ~SmartController();

    bool pull(std::shared_ptr<NetPacket>& pkt) override;

    void received(std::shared_ptr<RadioPacket>&& pkt) override;

    void retransmit(SendWindow &sendw);

    void ack(RecvWindow &recvw);

    void broadcastHello(void);

    /** @brief Get the controller's splice queue. */
    std::shared_ptr<NetSpliceQueue> getSpliceQueue(void)
    {
        return spliceq_;
    }

    /** @brief Set the controller's splice queue. */
    void setSpliceQueue(std::shared_ptr<NetSpliceQueue> q)
    {
        spliceq_ = q;
    }

protected:
    /** @brief Splice queue used to insert packets */
    std::shared_ptr<NetSpliceQueue> spliceq_;

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

    /** @brief Start the re-transmission timer if it is not set. */
    void startRetransmissionTimer(SendWindow &sendw);

    /** @brief Start the ACK timer if it is not set. */
    void startACKTimer(RecvWindow &recvw);

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
    RecvWindow &getReceiveWindow(NodeId node_id, Seq seq);
};

#endif /* SMARTCONTROLLER_H_ */
