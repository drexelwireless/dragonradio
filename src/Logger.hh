// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef LOGGER_H_
#define LOGGER_H_

#include <time.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <uhd/usrp/multi_usrp.hpp>
#include <H5Cpp.h>

#include "buffer.hh"
#include "Clock.hh"
#include "ExtensibleDataSet.hh"
#include "IQBuffer.hh"
#include "Packet.hh"
#include "SafeQueue.hh"
#include "mac/Snapshot.hh"

class Logger;

/** @brief The global logger. */
extern std::shared_ptr<Logger> logger;

class Logger {
public:
    /** @brief Logging sources */
    enum Source {
        kSlots = 0,
        kTXRecords,
        kRecvPackets,
        kRecvSymbols,
        kSentPackets,
        kSentIQ,
        kEvents,
        kARQEvents
    };

    Logger(const WallClock::time_point &t_start,
           const MonoClock::time_point &mono_t_start);
    ~Logger();

    Logger() = delete;
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;

    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;

    void open(const std::string& filename);
    void stop(void);
    void close(void);

    inline bool getCollectSource(Source src)
    {
        return sources_ & (1 << src);
    }

    void setCollectSource(Source src, bool collect)
    {
        if (collect)
            sources_ |= 1 << src;
        else
            sources_ &= ~(1 << src);
    }

    void setAttribute(const std::string& name, const std::string& val);
    void setAttribute(const std::string& name, uint8_t val);
    void setAttribute(const std::string& name, uint32_t val);
    void setAttribute(const std::string& name, int64_t val);
    void setAttribute(const std::string& name, uint64_t val);
    void setAttribute(const std::string& name, double val);

    void logSlot(const std::shared_ptr<IQBuf> &buf)
    {
        if (getCollectSource(kSlots)) {
            // Only log slots we haven't logged before. We should never be asked to log
            // a slot that is older than the youngest slot we've ever logged.
            if (buf->timestamp > t_last_slot_) {
                log_q_.push([=](){ logSlot_(*buf); });
                t_last_slot_ = *buf->timestamp;
            }
        }
    }

    void logTXRecord(const std::optional<MonoClock::time_point> &t, size_t nsamples, double fs)
    {
        if (getCollectSource(kTXRecords))
            log_q_.push([=](){ logTXRecord_(t, nsamples, fs); });
    }

    void logSnapshot(std::shared_ptr<Snapshot> snapshot)
    {
        log_q_.push([=](){ logSnapshot_(snapshot); });
    }

    void logRecv(const std::shared_ptr<RadioPacket> &pkt)
    {
        if (getCollectSource(kRecvPackets))
            log_q_.push([=, pkt = std::shared_ptr<RadioPacket>(pkt)]() { logRecv_(*pkt); });
    }

    void logSend(const std::shared_ptr<NetPacket> &pkt)
    {
        if (getCollectSource(kSentPackets))
            log_q_.push([=, pkt = std::shared_ptr<NetPacket>(pkt)]() { logSend_(pkt->tx_timestamp, kNotDropped, *pkt); });
    }

    void logLinkLayerDrop(const MonoClock::time_point& t,
                          const std::shared_ptr<NetPacket> &pkt)
    {
        if (getCollectSource(kSentPackets))
            log_q_.push([=, pkt = std::shared_ptr<NetPacket>(pkt)](){ logSend_(t, kLinkLayerDrop, *pkt); });
    }

    void logQueueDrop(const MonoClock::time_point& t,
                      const std::shared_ptr<NetPacket> &pkt)
    {
        if (getCollectSource(kSentPackets))
            log_q_.push([=, pkt = std::shared_ptr<NetPacket>(pkt)](){ logSend_(t, kQueueDrop, *pkt); });
    }

    void logEvent(const MonoClock::time_point& t,
                  const std::string& event)
    {
        if (getCollectSource(kEvents)){
            std::unique_ptr<char[]> buf(new char[event.length() + 1]);

            event.copy(&buf[0], event.length(), 0);
            buf[event.length()] = '\0';

            log_q_.push([=, event = buf.release()](){ logEvent_(t, event); });
        }
    }

    void logEvent(const MonoClock::time_point& t,
                  std::unique_ptr<char[]> event)
    {
        if (getCollectSource(kEvents))
            log_q_.push([=, event = event.release()](){ logEvent_(t, event); });
    }

    void logSendNAK(NodeId node,
                    Seq seq)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=](){ logARQEvent_(MonoClock::now(), kSendNAK, node, seq); });
    }

    void logSendSACK(const std::shared_ptr<NetPacket> &pkt,
                     NodeId node,
                     Seq unack)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=, pkt = std::shared_ptr<NetPacket>(pkt)](){ logARQSACKEvent_(*pkt, kSendSACK, node, unack); });
    }

    void logNAK(const MonoClock::time_point& t,
                NodeId node,
                Seq seq)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=](){ logARQEvent_(t, kNAK, node, seq); });
    }

    void logRetransmissionNAK(const MonoClock::time_point& t,
                              NodeId node,
                              Seq seq)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=](){ logARQEvent_(t, kRetransmissionNAK, node, seq); });
    }

    void logSACK(const std::shared_ptr<RadioPacket> &pkt,
                 NodeId node,
                 Seq unack)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=, pkt = std::shared_ptr<RadioPacket>(pkt)](){ logARQSACKEvent_(*pkt, kSACK, node, unack); });
    }

    void logSNAK(const MonoClock::time_point& t,
                 NodeId node,
                 Seq seq)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=](){ logARQEvent_(t, kSNAK, node, seq); });
    }

    void logACKTimeout(const MonoClock::time_point& t,
                       NodeId node,
                       Seq seq)
    {
        if (getCollectSource(kARQEvents))
            log_q_.push([=](){ logARQEvent_(t, kACKTimeout, node, seq); });
    }

private:
    bool is_open_;
    H5::H5File file_;
    std::unique_ptr<ExtensibleDataSet> slots_;
    std::unique_ptr<ExtensibleDataSet> tx_records_;
    std::unique_ptr<ExtensibleDataSet> snapshots_;
    std::unique_ptr<ExtensibleDataSet> selftx_;
    std::unique_ptr<ExtensibleDataSet> recv_;
    std::unique_ptr<ExtensibleDataSet> send_;
    std::unique_ptr<ExtensibleDataSet> event_;
    std::unique_ptr<ExtensibleDataSet> arq_event_;
    WallClock::time_point t_start_;
    MonoClock::time_point mono_t_start_;
    MonoClock::time_point t_last_slot_;

    /** @brief Data sources we collect. */
    uint32_t sources_;

    /** @brief Flag indicating we should terminate the logger. */
    bool done_;

    /** @brief Pending log entries. */
    SafeQueue<std::function<void(void)>> log_q_;

    /** @brief Log worker thread. */
    std::thread worker_thread_;

    /** @brief Log worker. */
    void worker(void);

    H5::Attribute createOrOpenAttribute(const std::string &name,
                                        const H5::DataType &data_type,
                                        const H5::DataSpace &data_space);

    void logSlot_(const IQBuf &buf);

    void logTXRecord_(const std::optional<MonoClock::time_point> &t, size_t nsamples, double fs);

    void logSnapshot_(std::shared_ptr<Snapshot> snapshot);

    void logRecv_(RadioPacket &pkt);

    enum DropType {
        kNotDropped = 0,
        kLinkLayerDrop,
        kQueueDrop
    };

    void logSend_(const MonoClock::time_point& t,
                  DropType dropped,
                  NetPacket &pkt);

    void logEvent_(const MonoClock::time_point& t,
                   char *event);

    enum ARQEventType {
        kSendNAK = 0,
        kSendSACK,
        kNAK,
        kRetransmissionNAK,
        kSACK,
        kSNAK,
        kACKTimeout,
    };

    void logARQEvent_(const MonoClock::time_point& t,
                      ARQEventType type,
                      NodeId node,
                      Seq seq);

    void logARQSACKEvent_(Packet &pkt,
                          ARQEventType type,
                          NodeId node,
                          Seq unack);
};

#endif /* LOGGER_H_ */
