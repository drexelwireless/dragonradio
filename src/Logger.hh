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
#include "SafeQueue.hh"
#include "phy/PHY.hh"

class Logger;

/** @brief The global logger. */
extern std::shared_ptr<Logger> logger;

class Logger {
public:
    Logger(Clock::time_point t_start);
    ~Logger();

    void open(const std::string& filename);

    void setAttribute(const std::string& name, const std::string& val);
    void setAttribute(const std::string& name, const uint8_t val);
    void setAttribute(const std::string& name, const uint32_t val);
    void setAttribute(const std::string& name, const double val);

    void logSlot(std::shared_ptr<IQBuf> buf);
    void logRecv(const Clock::time_point& t,
                 uint32_t start_samples,
                 uint32_t end_samples,
                 bool header_valid,
                 bool payload_valid,
                 const Header& hdr,
                 crc_scheme crc,
                 fec_scheme fec0,
                 fec_scheme fec1,
                 modulation_scheme ms,
                 float evm,
                 float rssi,
                 std::shared_ptr<buffer<std::complex<float>>> buf);
     void logSend(const Clock::time_point& t,
                  const Header& hdr,
                  std::shared_ptr<IQBuf> buf);

    void stop(void);

private:
    H5::H5File file_;
    std::unique_ptr<ExtensibleDataSet> slots_;
    std::unique_ptr<ExtensibleDataSet> recv_;
    std::unique_ptr<ExtensibleDataSet> send_;
    Clock::time_point t_start_;
    Clock::time_point t_last_slot_;

    /** @brief Flag indicating we should terminate the logger. */
    bool done_;

    /** @brief Pending log entries. */
    SafeQueue<std::function<void(void)>> log_q_;

    /** @brief Log worker thread. */
    std::thread worker_thread_;

    /** @brief Log worker. */
    void worker(void);

    void logSlot_(std::shared_ptr<IQBuf> buf);
    void logRecv_(const Clock::time_point& t,
                  uint32_t start_samples,
                  uint32_t end_samples,
                  bool header_valid,
                  bool payload_valid,
                  const Header& hdr,
                  crc_scheme crc,
                  fec_scheme fec0,
                  fec_scheme fec1,
                  modulation_scheme ms,
                  float evm,
                  float rssi,
                  std::shared_ptr<buffer<std::complex<float>>> buf);
    void logSend_(const Clock::time_point& t,
                  const Header& hdr,
                  std::shared_ptr<IQBuf> buf);
};
#endif /* LOGGER_H_ */
