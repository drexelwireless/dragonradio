#ifndef FDCHANNELIZER_H_
#define FDCHANNELIZER_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "barrier.hh"
#include "ringbuffer.hh"
#include "spinlock_mutex.hh"
#include "Logger.hh"
#include "dsp/FFTW.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"

/** @brief A frequency-domain channelizer. */
class FDChannelizer : public Channelizer
{
public:
    /** @brief Filter length */
    /** We need two factors of 5 because we need to support 25MHz bandwidth.
     * The rest of the factors of 2 are for good measure.
     */
    static constexpr unsigned P = 25*64+1;

    /** @brief Overlap factor */
    static constexpr unsigned V = 4;

    /** @brief Length of FFT */
    static constexpr unsigned N = V*(P-1);

    /** @brief Size of FFT overlap */
    static constexpr unsigned O = P-1;

    /** @brief Number of new samples consumed per input block */
    static constexpr unsigned L = N - (P-1);

    FDChannelizer(std::shared_ptr<PHY> phy,
                  double rx_rate,
                  const Channels &channels,
                  unsigned int nthreads);
    virtual ~FDChannelizer();

    void push(const std::shared_ptr<IQBuf> &) override;

    void reconfigure(void) override;

    /** @brief Stop demodulating. */
    void stop(void);

private:
    /** @brief Log of size of ring buffers */
    static constexpr unsigned LOGR = 4;

    /** @brief Channel state for time-domain demodulation */
    class ChannelState {
    public:
        ChannelState(PHY &phy,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double rate);

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
                       std::optional<size_t> snapshot_off,
                       size_t offset);

        /** @brief Demodulate data with given parameters */
        void demodulate(const std::complex<float>* data,
                        size_t count,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback);

    protected:
        /** @brief Channel we are demodulating */
        Channel channel_;

        /** @brief Resampling rate */
        double rate_;

        /** @brief Oversample factor */
        unsigned X_;

        /** @brief Decimation factor */
        unsigned D_;

        /** @brief Number of FFT bins to rotate */
        int Nrot_;

        /** @brief IFFT */
        fftw::FFT<C> ifft_;

        /** @brief Frequency-domain filter */
        fftw::vector<C> H_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::Demodulator> demod_;

        /** @brief Channel IQ buffer sequence number */
        unsigned seq_;
    };

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

    /** @brief Time-domain packets to demodulate */
    ringbuffer<std::shared_ptr<IQBuf>, LOGR> iqbuf_;

    /** @brief Mutex for demodulation state. */
    spinlock_mutex demod_mutex_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<ChannelState>> demods_;

    /** @brief A pair of time- and frequency-domain IQ buffers */
    using bufpair = std::pair<std::shared_ptr<IQBuf>, std::shared_ptr<IQBuf>>;

    /** @brief Frequency-domain packets to demodulate */
    std::unique_ptr<ringbuffer<bufpair, LOGR> []> fdiqbufs_;

    /** @brief FFT worker thread. */
    std::thread fft_thread_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Worker that converts packets from time domain to frequency
     * domain.
     */
    void fftWorker(void);

    /** @brief A demodulation worker. */
    void demodWorker(unsigned tid);
};

#endif /* FDCHANNELIZER_H_ */
