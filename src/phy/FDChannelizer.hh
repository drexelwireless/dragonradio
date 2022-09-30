// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FDCHANNELIZER_H_
#define FDCHANNELIZER_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "Logger.hh"
#include "SafeQueue.hh"
#include "dsp/FDResampler.hh"
#include "dsp/FFTW.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"

/** @brief A frequency-domain channelizer. */
class FDChannelizer : public Channelizer
{
public:
    using C = std::complex<float>;

    using Resampler = dragonradio::signal::FDResampler<C>;

    static constexpr auto P = Resampler::P;
    static constexpr auto V = Resampler::V;
    static constexpr auto N = Resampler::N;
    static constexpr auto O = Resampler::O;
    static constexpr auto L = Resampler::L;

    FDChannelizer(const std::vector<PHYChannel> &channels,
                  double rx_rate,
                  unsigned int nthreads);
    virtual ~FDChannelizer();

    void setChannels(const std::vector<PHYChannel> &channels) override;

    void setRXRate(double rate) override;

    void push(const std::shared_ptr<IQBuf> &) override;

    /** @brief Stop demodulating. */
    void stop(void);

private:
    /** @brief Channel state for time-domain demodulation */
    class FDChannelDemodulator : public ChannelDemodulator {
    public:
        FDChannelDemodulator(unsigned chanidx,
                             const PHYChannel &channel,
                             double rate);

        virtual ~FDChannelDemodulator() = default;

        /** @brief Update IQ buffer sequence number */
        void updateSeq(unsigned seq);

        void reset(void) override;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset) override;

        void demodulate(const std::complex<float>* data,
                        size_t count) override;

    protected:
        /** @brief Channel IQ buffer sequence number */
        unsigned seq_;

        /** @brief Frequency-domain resampler */
        Resampler resampler_;
    };

    /** @brief A demodulation slot */
    struct Slot {
        Slot() = default;

        // So we can emplace
        Slot(const std::shared_ptr<IQBuf>& iqbuf_,
             const std::shared_ptr<IQBuf>& fdbuf_,
             ssize_t fd_offset_) noexcept
          : iqbuf(iqbuf_)
          , fdbuf(fdbuf_)
          , fd_offset(fd_offset_)
        {
        }

        /** @brief The slot's time-domain samples */
        std::shared_ptr<IQBuf> iqbuf;

        /** @brief The slot's frequency-domain samples */
        std::shared_ptr<IQBuf> fdbuf;

        /** @brief Offset of frequency-domain samples from time-domain samples
         * (in samples)
         */
        /** This is used to account for the fact that the frequency-domain
         * buffer may hold some samples from the previous slot's time-domain
         * buffer that didn't fit in a full size N FFT.
         */
        ssize_t fd_offset;
    };

    /** @brief Number of demodulation threads. */
    unsigned nthreads_;

    /** @brief Time-domain IQ buffers to demodulate */
    SafeQueue<std::shared_ptr<IQBuf>> tdbufs_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<FDChannelDemodulator>> demods_;

    /** @brief Frequency-domain packets to demodulate */
    std::vector<std::unique_ptr<SafeQueue<Slot>>> slots_;

    /** @brief FFT worker thread. */
    std::thread fft_thread_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Check that channels are compatible with bandwidth specification */
    void checkChannels(const std::vector<PHYChannel> &channels, double rx_rate);

    /** @brief Worker that converts packets from time domain to frequency
     * domain.
     */
    void fftWorker(void);

    /** @brief A demodulation worker. */
    void demodWorker(unsigned tid);

    void reconfigure(void) override;

    void wake_dependents() override;
};

#endif /* FDCHANNELIZER_H_ */
