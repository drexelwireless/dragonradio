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
#include "ExtensibleDataSet.hh"
#include "IQBuffer.hh"
#include "Node.hh"
#include "SafeQueue.hh"
#include "phy/PHY.hh"

class Logger {
public:
    Logger(const std::string& filename,
           NodeId node_id);
    ~Logger();

    void setTXBandwidth(double bw);
    void setRXBandwidth(double bw);

    void stop(void);

    void logSlot(std::shared_ptr<IQBuf> buf);
    void logRecv(const Clock::time_point& t,
                 bool header_valid,
                 bool payload_valid,
                 const Header& hdr,
                 uint32_t start_samples,
                 uint32_t end_samples,
                 std::shared_ptr<buffer<std::complex<float>>> buf);
     void logSend(const Clock::time_point& t,
                  const Header& hdr,
                  std::shared_ptr<IQBuf> buf);

private:
    H5::H5File _file;
    std::unique_ptr<ExtensibleDataSet> _slots;
    std::unique_ptr<ExtensibleDataSet> _recv;
    std::unique_ptr<ExtensibleDataSet> _send;
    Clock::time_point _t_start;
    Clock::time_point _t_last_slot;

    /** @brief Flag indicating we should terminate the logger. */
    bool _done;

    /** @brief Pending log entries. */
    SafeQueue<std::function<void(void)>> log_q;

    /** @brief Log worker thread. */
    std::thread worker_thread;

    /** @brief Log worker. */
    void worker(void);

    void _logSlot(std::shared_ptr<IQBuf> buf);
    void _logRecv(const Clock::time_point& t,
                  bool header_valid,
                  bool payload_valid,
                  const Header& hdr,
                  uint32_t start_samples,
                  uint32_t end_samples,
                  std::shared_ptr<buffer<std::complex<float>>> buf);
    void _logSend(const Clock::time_point& t,
                  const Header& hdr,
                  std::shared_ptr<IQBuf> buf);
};
#endif /* LOGGER_H_ */
