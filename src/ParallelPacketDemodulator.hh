#ifndef PARALLELPACKETDEMODULATOR_H_
#define PARALLELPACKETDEMODULATOR_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "NET.hh"
#include "PacketDemodulator.hh"
#include "RadioPacketQueue.hh"
#include "phy/PHY.hh"

/** @brief A thread-safe queue of IQ buffers that need to be demodulated. */
class IQBufQueue {
public:
    IQBufQueue(RadioPacketQueue& radio_q);
    ~IQBufQueue();

    IQBufQueue(const IQBufQueue&) = delete;
    IQBufQueue(IQBufQueue&&) = delete;

    IQBufQueue& operator=(const IQBufQueue&) = delete;
    IQBufQueue& operator=(IQBufQueue&&) = delete;

    /** @brief Add an IQ buffer to the queue. */
    void push(std::shared_ptr<IQBuf> buf);

    /** @brief Get two slot's worth of IQ data.
     * @param b The barrier before which network packets should be inserted.
     * @param buf1 The buffer holding the previous slot's IQ data.
     * @param buf2 The buffer holding the current slot's IQ data.
     * @return Return true if pop was successful, false otherwise.
     */
    /** Return two slot's worth of IQ data---the previous slot, and the current
     * slot. The previous slot is removed from the queue, whereas the current
     * slot is kept in the queue because it becomes the new "previous" slot.
     */
    bool pop(RadioPacketQueue::barrier& b,
             std::shared_ptr<IQBuf>& buf1,
             std::shared_ptr<IQBuf>& buf2);

    /** @brief Stop processing this queue. */
    void stop(void);

private:
    /** @brief The queue on which demodulated packets should be placed. */
    RadioPacketQueue& _radio_q;

    /** @brief Flag that is true when we should finish processing. */
    bool _done;

    /** @brief Mutex protecting the queue of IQ buffers. */
    std::mutex _m;

    /** @brief Condition variable protecting the queue of IQ buffers. */
    std::condition_variable _cond;

    /** @brief The number of items in the queue of IQ buffers. */
    size_t _size;

    /** @brief The queue of IQ buffers. */
    std::list<std::shared_ptr<IQBuf>> _q;
};

/** @brief A parallel packet demodulator. */
class ParallelPacketDemodulator : public PacketDemodulator
{
public:
    ParallelPacketDemodulator(std::shared_ptr<NET> net,
                              std::shared_ptr<PHY> phy,
                              bool order,
                              unsigned int nthreads);
    virtual ~ParallelPacketDemodulator();

    /** @brief Set demodulation parameters.
     * @brief prev_samps The number of samples from the end of the previous slot
     * to demodulate.
     * @brief cur_samps The number of samples from the current slot to
     * demodulate.
     */
    void setDemodParameters(const size_t prev_samps,
                            const size_t cur_samps) override;

    /** @brief Add an IQ buffer to demodulate. */
    void push(std::shared_ptr<IQBuf> buf) override;

    /** @brief Stop demodulating. */
    void stop(void);

private:
    /** @brief Destination for packets. */
    std::shared_ptr<NET> net;

    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy;

    /** @brief Should packets be output in order of reception? This increases
     * latency.
     */
    bool _order;

    /** @brief Number of samples to demod from tail of previous slot. */
    size_t _prev_samps;

    /** @brief Number of samples NOT to demod from tail of current slot. */
    size_t _cur_samps;

    /** @brief Flag that is true when we should finish processing. */
    bool done;

    /** @brief IQ buffers we need to demodulate. */
    IQBufQueue demod_q;

    /** @brief Queue of radio packets. */
    RadioPacketQueue radio_q;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads;

    /** @brief A demodulation worker. */
    void demod_worker(void);

    /** @brief Network send thread. */
    std::thread net_thread;

    /** @brief The network wend worker. */
    void net_worker(void);
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
