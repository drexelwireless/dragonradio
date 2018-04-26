#ifndef RADIOPACKETSINK_H_
#define RADIOPACKETSINK_H_

#include <memory>
#include <thread>

#include "NET.hh"
#include "Packet.hh"
#include "SafeQueue.hh"

class RadioPacketSink {
public:
    RadioPacketSink(std::shared_ptr<NET> net);
    virtual ~RadioPacketSink();

    /** @brief Halt packet processing. */
    void stop(void);

    /** @brief Return true if we want a packet sent to this destination. */
    bool wantPacket(NodeId dest);

    /** @brief Push a packet up to the network. */
    void push(std::unique_ptr<RadioPacket> pkt);

private:
    /** @brief The NET object where we should send packets. */
    std::shared_ptr<NET> _net;

    /** @brief Flag indicating when we should finish acting as a sink. */
    bool _done;

    /** Queue of RadioPacket@s to send. */
    SafeQueue<std::unique_ptr<RadioPacket>> _q;

    /** @brief Thread runnign worker. */
    std::thread _worker_thread;

    /** @brief Worker that sends packets to the NET. */
    void worker(void);
};

#endif /* RADIOPACKETSINK_H_ */
