// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MULTICHANNELSYNTHESIZER_H_
#define MULTICHANNELSYNTHESIZER_H_

#include <atomic>
#include <mutex>

#include "dsp/FDUpsampler.hh"
#include "dsp/FFTW.hh"
#include "phy/PHY.hh"
#include "phy/SlotSynthesizer.hh"

/** @brief A frequency-domain, per-channel synthesizer. */
class MultichannelSynthesizer : public SlotSynthesizer
{
public:
    using Upsampler = dragonradio::signal::FDUpsampler<C>;

    static constexpr auto P = Upsampler::P;
    static constexpr auto N = Upsampler::N;
    static constexpr auto L = Upsampler::L;
    static constexpr auto O = Upsampler::O;

    MultichannelSynthesizer(const std::vector<PHYChannel> &channels,
                            double tx_rate,
                            size_t nthreads);
    virtual ~MultichannelSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void finalize(Slot &slot) override;

    void stop(void) override;

private:
    /** @brief Channel modulator for multichannel modulation */
    class MultichannelModulator : public ChannelModulator, private Upsampler {
    public:
        MultichannelModulator(const PHYChannel &channel,
                              unsigned chanidx,
                              double tx_rate);
        MultichannelModulator() = delete;

        ~MultichannelModulator() = default;

        void modulate(std::shared_ptr<NetPacket> pkt,
                      const float g,
                      ModPacket &mpkt) override;

        /** @brief Specify the next slot to modulate.
         * @param prev_slot The previous slot
         * @param slot The new slot
         * @param overfill true if this slot can be overfilled
         */
        void nextSlot(const Slot *prev_slot, Slot &slot, const bool overfill);

        /** @brief Determine whether or not a modulated packet will fit in
         * the current frequency domain buffer.
         * @param mpkt The modulated packet
         * @param overfill Flag that is true if the slot can be overfilled
         * @return true if the packet will fit in the slot, false otherwise
         */
        /** If the packet will fit in the slot, nsamples is updated
         * appropriately.
         */
        bool fits(ModPacket &mpkt, const bool overfill);

        /** @brief Set current IQ buffer to be upsampled.
         * @param iqbuf
         */
        void setIQBuffer(std::shared_ptr<IQBuf> &&iqbuf);

        /** @brief Calculate how many samples will be in upsampled signal.
         * @@param n Number of samples in original signal.
         * @return Number of samples in upsampled signal.
         */
        size_t upsampledSize(size_t n)
        {
            return I*n/X;
        }

        /** @brief Perform frequency-domain upsampling on current IQ buffer.
         * @return Number of samples read from the input buffer.
         */
        size_t upsample(void);

        /** @brief Perform frequency-domain upsampling on current IQ buffer.
         * @param slot Current slot.
         */
        void flush(Slot &slot);

        /** @brief Mutex for channel state */
        std::mutex mutex;

        /** @brief Packet whose modulated signal is the IQ buffer */
        std::shared_ptr<NetPacket> pkt;

        /** @brief IQ buffer being upsampled */
        std::shared_ptr<IQBuf> iqbuf;

        /** @brief Offset of unmodulated data in IQ buffer */
        size_t iqbufoff;

        /** @brief Frequency domain buffer into which we upsample */
        IQBuf *fdbuf;

        /** @brief Number of time domain samples in the frequency domain buffer
         * to delay.
         */
        size_t delay;

        /** @brief Number of valid time-domain samples represented by data in
         * the frequency domain buffer.
         */
        /** This represents the number of valid UN-overlapped time-domain
         * samples represented by the FFT blocks in the frequency domain buffer.
         */
        size_t nsamples;

        /** @brief Maximum number of time-domain samples. */
        size_t max_samples;

        /** @brief Number of time-domain samples represented by final FFT block
         * that are included in nsamples.
         */
        size_t npartial;

        /** @brief FFT buffer offset before flush of partial block. */
        std::optional<size_t> partial_fftoff;

        /** @brief Number of valid samples in the frequency-domain buffer */
        /** This will be a multiple of N */
        size_t fdnsamples;
    };

    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<MultichannelModulator>> mods_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief OLS time domain converter */
    Upsampler::ToTimeDomain timedomain_;

    /** @brief Gain necessary to compensate for simultaneous transmission */
    float g_multichan_;

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);

    void reconfigure(void) override;

    void wake_dependents() override;
};

#endif /* MULTICHANNELSYNTHESIZER_H_ */
