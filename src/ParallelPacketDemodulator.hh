#ifndef PARALLELPACKETDEMODULATOR_H_
#define PARALLELPACKETDEMODULATOR_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "Logger.hh"
#include "NET.hh"
#include "phy/PHY.hh"

class ParallelPacketDemodulator
{
public:
    ParallelPacketDemodulator(std::shared_ptr<NET> net,
                              std::shared_ptr<PHY> phy,
                              std::shared_ptr<Logger> logger,
                              unsigned int nthreads);
    ~ParallelPacketDemodulator();

    void stop(void);

    /** @brief Set demodulation parameters.
     * @brief prev_samps The number of samples from the end of the previous slot
     * to demodulate.
     * @brief cur_samps The number of samples from the current slot to
     * demodulate.
     */
    void setDemodParameters(const size_t prev_samps,
                            const size_t cur_samps);

    void push(std::shared_ptr<IQBuf> buf);

private:
    /** @brief Destination for packets. */
    std::shared_ptr<NET> net;

    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy;

    /** @brief The Logger to use. Should be nullptr for no logging. */
    std::shared_ptr<Logger> logger;

    /** @brief Number of samples to demod from tail of previous slot. */
    size_t _prev_samps;

    /** @brief Number of samples NOT to demod from tail of current slot. */
    size_t _cur_samps;

    /** @brief Flag that is true when we shoudl finish processing. */
    bool done;

    /** @brief Mutex protecting the queue of IQ buffers. */
    std::mutex m;

    /** @brief Condition variable protecting the queue of IQ buffers. */
    std::condition_variable cond;

    /** @brief The number of items in the queue of IQ buffers. */
    size_t size;

    /** @brief The queue IQ buffers. */
    std::list<std::shared_ptr<IQBuf>> q;

    /** @brief Worker threads. */
    std::vector<std::thread> threads;

    /** @brief A demodulation worker. */
    void worker(void);
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
