#ifndef LOGGER_H_
#define LOGGER_H_

#include <stdarg.h>
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
    enum Source {
        kSlots = 0,
        kRecvPackets = 1,
        kRecvSymbols = 2,
        kSentPackets = 3,
        kSentIQ = 4,
        kEvents = 5
    };

    Logger(Clock::time_point t_start);
    ~Logger();

    Logger() = delete;
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;

    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;

    void open(const std::string& filename);
    void close(void);

    bool getCollectSource(Source src);
    void setCollectSource(Source src, bool collect);

    void setAttribute(const std::string& name, const std::string& val);
    void setAttribute(const std::string& name, uint8_t val);
    void setAttribute(const std::string& name, uint32_t val);
    void setAttribute(const std::string& name, double val);

    void logSlot(std::shared_ptr<IQBuf> buf,
                 float bw);

    void logSnapshot(std::shared_ptr<IQBuf> buf);

    void logSelfTX(Clock::time_point timestamp,
                   SelfTX pkt);

    void logRecv(const Clock::time_point& t,
                 int32_t start_samples,
                 int32_t end_samples,
                 bool header_valid,
                 bool payload_valid,
                 const Header& hdr,
                 NodeId src,
                 NodeId dest,
                 unsigned mcsidx,
                 float evm,
                 float rssi,
                 float cfo,
                 float fc,
                 float bw,
                 float demod_latency,
                 uint32_t size,
                 std::shared_ptr<buffer<std::complex<float>>> buf);

    void logSend(const Clock::time_point& t,
                 const Header& hdr,
                 NodeId src,
                 NodeId dest,
                 unsigned mcsidx,
                 float fc,
                 float bw,
                 uint32_t size,
                 std::shared_ptr<IQBuf> buf,
                 size_t offset,
                 size_t nsamples);

    void logEvent(const Clock::time_point& t,
                  const std::string& event);

    void stop(void);

private:
    bool is_open_;
    H5::H5File file_;
    std::unique_ptr<ExtensibleDataSet> slots_;
    std::unique_ptr<ExtensibleDataSet> snapshots_;
    std::unique_ptr<ExtensibleDataSet> selftx_;
    std::unique_ptr<ExtensibleDataSet> recv_;
    std::unique_ptr<ExtensibleDataSet> send_;
    std::unique_ptr<ExtensibleDataSet> event_;
    Clock::time_point t_start_;
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

    void logSnapshot_(std::shared_ptr<IQBuf> buf);

    void logSelfTX_(Clock::time_point timestamp,
                    SelfTX pkt);

    void logRecv_(const Clock::time_point& t,
                  int32_t start_samples,
                  int32_t end_samples,
                  bool header_valid,
                  bool payload_valid,
                  const Header& hdr,
                  NodeId src,
                  NodeId dest,
                  unsigned mcsidx,
                  float evm,
                  float rssi,
                  float cfo,
                  float fc,
                  float bw,
                  float demod_latency,
                  uint32_t size,
                  std::shared_ptr<buffer<std::complex<float>>> buf);

    void logSend_(const Clock::time_point& t,
                  const Header& hdr,
                  NodeId src,
                  NodeId dest,
                  unsigned mcsidx,
                  float fc,
                  float bw,
                  uint32_t size,
                  std::shared_ptr<IQBuf> buf,
                  size_t offset,
                  size_t nsamples);

     void logEvent_(const Clock::time_point& t,
                    const std::string& event);
};

void vlogEvent(const Clock::time_point& t, const char *fmt, va_list ap);

void logEventAt(const Clock::time_point& t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

inline void logEventAt(const Clock::time_point& t, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vlogEvent(t, fmt, ap);
    va_end(ap);
}

void logEvent(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

inline void logEvent(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vlogEvent(Clock::now(), fmt, ap);
    va_end(ap);
}

#endif /* LOGGER_H_ */
