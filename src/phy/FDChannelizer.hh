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
    class FDChannelDemodulator;
    struct Slot;

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
