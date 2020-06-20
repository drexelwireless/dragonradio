#ifndef TDCHANNELIZER_H_
#define TDCHANNELIZER_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "barrier.hh"
#include "ringbuffer.hh"
#include "spinlock_mutex.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"

/** @brief A time-domain channelizer. */
class TDChannelizer : public Channelizer
{
public:
    TDChannelizer(std::shared_ptr<PHY> phy,
                  double rx_rate,
                  const Channels &channels,
                  unsigned int nthreads);
    virtual ~TDChannelizer();

    void push(const std::shared_ptr<IQBuf> &) override;

    void reconfigure(void) override;

    /** @brief Stop demodulating. */
    void stop(void);

private:
    /** @brief Channel state for time-domain demodulation */
    class ChannelState {
    public:
        ChannelState(PHY &phy,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double rx_rate);

        ~ChannelState() = default;

        /** @brief Update IQ buffer sequence number */
        void updateSeq(unsigned seq);

        /** @brief Reset internal state */
        void reset(void);

        /** @brief Set timestamp for demodulation
         * @param timestamp The timestamp for future samples.
         * @param snapshot_off The snapshot offset associated with the given
         * timestamp.
         * @param offset The offset of the first sample that will be demodulated.
         */
        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       size_t offset);

        /** @brief Demodulate data with given parameters */
        void demodulate(IQBuf &resamp_buf,
                        const std::complex<float>* data,
                        size_t count,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback);

    protected:
        /** @brief Channel we are demodulating */
        Channel channel_;

        /** @brief Resampling rate */
        double rate_;

        /** @brief Frequency shift in radians, i.e., 2*M_PI*shift/Fs */
        double rad_;

        /** @brief Resampler */
        Dragon::MixingRationalResampler<C,C> resamp_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::PacketDemodulator> demod_;

        /** @brief Channel IQ buffer sequence number */
        unsigned seq_;
    };

    static const unsigned LOGN = 4;

    /** @brief Number of demodulation threads. */
    unsigned nthreads_;

    /** @brief Flag that is true when we should finish processing. */
    bool done_;

    /** @brief Flag that is true when we are reconfiguring. */
    std::atomic<bool> reconfigure_;

    /** @brief Reconfiguration barrier */
    barrier reconfigure_sync_;

    /** @brief Mutex for waking demodulators. */
    std::mutex wake_mutex_;

    /** @brief Condition variable for waking demodulators. */
    std::condition_variable wake_cond_;

    /** @brief Mutex for demodulation state. */
    spinlock_mutex demod_mutex_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<ChannelState>> demods_;

    /** @brief Packets to demodulate */
    std::unique_ptr<ringbuffer<std::shared_ptr<IQBuf>, LOGN> []> iqbufs_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief A demodulation worker. */
    void demodWorker(unsigned tid);
};

#endif /* TDCHANNELIZER_H_ */
