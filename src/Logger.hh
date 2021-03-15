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
        kRecvPackets = 1,
        kRecvSymbols = 2,
        kSentPackets = 3,
        kSentIQ = 4,
        kEvents = 5
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

    void logSlot(std::shared_ptr<IQBuf> buf,
                 float bw);

    void logSnapshot(std::shared_ptr<Snapshot> snapshot)
    {
        log_q_.push([=](){ logSnapshot_(snapshot); });
    }

    void logRecv(const MonoClock::time_point& t,
                 int32_t start_samples,
                 int32_t end_samples,
                 bool header_valid,
                 bool payload_valid,
                 const Header& hdr,
                 const ExtendedHeader& ehdr,
                 uint32_t mgen_flow_uid,
                 uint32_t mgen_seqno,
                 unsigned mcsidx,
                 float evm,
                 float rssi,
                 float cfo,
                 float fc,
                 float bw,
                 float demod_latency,
                 uint32_t size,
                 buffer<std::complex<float>> *buf)
    {
        if (getCollectSource(kRecvPackets))
            log_q_.push([=](){ logRecv_(t, start_samples, end_samples, header_valid, payload_valid, hdr, ehdr, mgen_flow_uid, mgen_seqno, mcsidx, evm, rssi, cfo, fc, bw, demod_latency, size, buf); });
    }

    void logSend(const MonoClock::time_point& t,
                 unsigned nretrans,
                 const Header& hdr,
                 const ExtendedHeader& ehdr,
                 uint32_t mgen_flow_uid,
                 uint32_t mgen_seqno,
                 unsigned mcsidx,
                 float fc,
                 float bw,
                 double mod_latency,
                 uint32_t size,
                 std::shared_ptr<IQBuf> buf,
                 size_t offset,
                 size_t nsamples)
    {
        if (getCollectSource(kSentPackets))
            log_q_.push([=](){ logSend_(t, kNotDropped, nretrans, hdr, ehdr, mgen_flow_uid, mgen_seqno, mcsidx, fc, bw, mod_latency, size, buf, offset, nsamples); });
    }

    void logLinkLayerDrop(const MonoClock::time_point& t,
                          unsigned nretrans,
                          const Header& hdr,
                          const ExtendedHeader& ehdr,
                          uint32_t mgen_flow_uid,
                          uint32_t mgen_seqno,
                          unsigned mcsidx,
                          uint32_t size)
    {
        if (getCollectSource(kSentPackets))
            log_q_.push([=](){ logSend_(t, kLinkLayerDrop, nretrans, hdr, ehdr, mgen_flow_uid, mgen_seqno, mcsidx, 0, 0, 0, size, nullptr, 0, 0); });
    }

    void logQueueDrop(const MonoClock::time_point& t,
                      unsigned nretrans,
                      const Header& hdr,
                      const ExtendedHeader& ehdr,
                      uint32_t mgen_flow_uid,
                      uint32_t mgen_seqno,
                      unsigned mcsidx,
                      uint32_t size)
    {
        if (getCollectSource(kSentPackets))
            log_q_.push([=](){ logSend_(t, kQueueDrop, nretrans, hdr, ehdr, mgen_flow_uid, mgen_seqno, mcsidx, 0, 0, 0, size, nullptr, 0, 0); });
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

private:
    bool is_open_;
    H5::H5File file_;
    std::unique_ptr<ExtensibleDataSet> slots_;
    std::unique_ptr<ExtensibleDataSet> snapshots_;
    std::unique_ptr<ExtensibleDataSet> selftx_;
    std::unique_ptr<ExtensibleDataSet> recv_;
    std::unique_ptr<ExtensibleDataSet> send_;
    std::unique_ptr<ExtensibleDataSet> event_;
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

    void logSlot_(std::shared_ptr<IQBuf> buf,
                  float bw);

    void logSnapshot_(std::shared_ptr<Snapshot> snapshot);

    void logRecv_(const MonoClock::time_point& t,
                  int32_t start_samples,
                  int32_t end_samples,
                  bool header_valid,
                  bool payload_valid,
                  const Header& hdr,
                  const ExtendedHeader& ehdr,
                  uint32_t mgen_flow_uid,
                  uint32_t mgen_seqno,
                  unsigned mcsidx,
                  float evm,
                  float rssi,
                  float cfo,
                  float fc,
                  float bw,
                  float demod_latency,
                  uint32_t size,
                  buffer<std::complex<float>> *buf);

    enum DropType {
        kNotDropped = 0,
        kLinkLayerDrop,
        kQueueDrop
    };

    void logSend_(const MonoClock::time_point& t,
                  DropType dropped,
                  unsigned nretrans,
                  const Header& hdr,
                  const ExtendedHeader& ehdr,
                  uint32_t mgen_flow_uid,
                  uint32_t mgen_seqno,
                  unsigned mcsidx,
                  float fc,
                  float bw,
                  double mod_latency,
                  uint32_t size,
                  std::shared_ptr<IQBuf> buf,
                  size_t offset,
                  size_t nsamples);

     void logEvent_(const MonoClock::time_point& t,
                    char *event);
};

#endif /* LOGGER_H_ */
