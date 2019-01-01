#ifndef PARALLELPACKETDEMODULATOR_H_
#define PARALLELPACKETDEMODULATOR_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "spinlock_mutex.hh"
#include "PacketDemodulator.hh"
#include "RadioPacketQueue.hh"
#include "phy/Channels.hh"
#include "phy/ModParams.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief Demodulation state. */
class DemodState {
public:
    DemodState(const Liquid::ResamplerParams &params,
               double signal_rate,
               double resamp_rate,
               double shift)
      : modparams(params,
                  signal_rate,
                  resamp_rate,
                  shift)
      , demod(nullptr)
    {
    }

    DemodState() = delete;
    DemodState(const DemodState&) = delete;
    DemodState(DemodState&&) = delete;

    ~DemodState() = default;

    DemodState &operator =(const DemodState&) = delete;
    DemodState &operator =(DemodState &&) = delete;

    /** @brief Channel demodulation parameters */
    ModParams modparams;

    /** @brief Channel demodulator */
    std::shared_ptr<PHY::Demodulator> demod;

   /** @brief Demodulate data with given parameters */
   void demodulate(IQBuf &shift_buf,
                   IQBuf &resamp_buf,
                   const std::complex<float>* data,
                   size_t count,
                   std::function<void(std::unique_ptr<RadioPacket>)> callback);
};

/** @brief A parallel packet demodulator. */
class ParallelPacketDemodulator : public PacketDemodulator, public Element
{
public:
    ParallelPacketDemodulator(std::shared_ptr<Net> net,
                              std::shared_ptr<PHY> phy,
                              const Channels &channels,
                              unsigned int nthreads);
    virtual ~ParallelPacketDemodulator();

    void setChannels(const Channels &channels) override;

    void setWindowParameters(const size_t prev_samps,
                             const size_t cur_samps) override;

    void push(std::shared_ptr<IQBuf> buf) override;

    void reconfigure(void) override;

    /** @brief Return flag indicating whether or not demodulation queue enforces
     * packet order.
     */
    bool getEnforceOrdering(void);

    /** @brief Set whether or not demodulation queue enforces packet order. */
    void setEnforceOrdering(bool enforce);

    /** @brief Stop demodulating. */
    void stop(void);

    /** @brief Demodulated packets */
    RadioOut<Push> source;

    /** @brief Resampler parameters for demodulator */
    Liquid::ResamplerParams downsamp_params;

private:
    /** @brief Destination for packets. */
    std::shared_ptr<Net> net_;

    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy_;

    /** @brief Should packets be output in the order they were actually
     * received? Setting this to true increases latency!
     */
    bool enforce_ordering_;

    /** @brief Number of samples to demod from tail of previous slot. */
    size_t prev_samps_;

    /** @brief Number of samples NOT to demod from tail of current slot. */
    size_t cur_samps_;

    /** @brief Flag that is true when we should finish processing. */
    bool done_;

    /** @brief Queue of radio packets. */
    RadioPacketQueue radio_q_;

    /** @brief Mutex protecting the queue of IQ buffers. */
    std::mutex iq_mutex_;

    /** @brief Condition variable protecting the queue of IQ buffers. */
    std::condition_variable iq_cond_;

    /** @brief The number of items in the queue of IQ buffers. */
    size_t iq_size_;

    /** @brief The next channel to demodulate. */
    Channels::size_type iq_next_channel_;

    /** @brief The queue of IQ buffers. */
    std::list<std::shared_ptr<IQBuf>> iq_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> demod_reconfigure_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief Network send thread. */
    std::thread net_thread_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief A demodulation worker. */
    void demodWorker(std::atomic<bool> &reconfig);

    /** @brief The network wend worker. */
    void netWorker(void);

    /** @brief Get two slot's worth of IQ data.
     * @param b The barrier before which network packets should be inserted.
     * @param channel The channel to demodulate.
     * @param buf1 The buffer holding the previous slot's IQ data.
     * @param buf2 The buffer holding the current slot's IQ data.
     * @return Return true if pop was successful, false otherwise.
     */
    /** Return two slot's worth of IQ data---the previous slot, and the current
     * slot. The previous slot is removed from the queue, whereas the current
     * slot is kept in the queue because it becomes the new "previous" slot.
     */
    bool pop(RadioPacketQueue::barrier& b,
             unsigned &channel,
             std::shared_ptr<IQBuf>& buf1,
             std::shared_ptr<IQBuf>& buf2);

     /** @brief Move to the next demodulation window. */
     void nextWindow(void);

    /** @brief Demodulate data with given parameters */
    void demodulateWithParams(DemodState &demod,
                              IQBuf &shift_buf,
                              IQBuf &resamp_buf,
                              const std::complex<float>* data,
                              size_t count,
                              std::function<void(std::unique_ptr<RadioPacket>)> callback);
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
