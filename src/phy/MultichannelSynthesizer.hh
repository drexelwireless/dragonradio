#ifndef FDCHANSYNTHESIZER_H_
#define FDCHANSYNTHESIZER_H_

#include <atomic>
#include <mutex>

#include "barrier.hh"
#include "dsp/FDResample.hh"
#include "dsp/FFTW.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "net/Net.hh"

/** @brief A frequency-domain, per-channel synthesizer. */
class MultichannelSynthesizer : public Synthesizer
{
public:
    using C = std::complex<float>;

    /** @brief Filter length */
    /** We need two factors of 5 because we need to support 25MHz bandwidth.
     * The rest of the factors of 2 are for good measure.
     */
    static constexpr unsigned P = 25*64+1;

    /** @brief Overlap factor */
    static constexpr unsigned V = 4;

    using Upsampler = FDUpsampler<C,P,V>;

    static constexpr auto N = Upsampler::N;
    static constexpr auto L = Upsampler::L;
    static constexpr auto O = Upsampler::O;

    MultichannelSynthesizer(std::shared_ptr<PHY> phy,
                            double tx_rate,
                            const Channels &channels,
                            size_t nthreads);
    virtual ~MultichannelSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void finalize(Slot &slot) override;

    void reconfigure(void) override;

    /** @brief Stop modulating. */
    void stop(void);

private:
    /** @brief Channel state for time-domain modulation */
    class ChannelState : private Upsampler {
    public:
        ChannelState(PHY &phy,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double tx_rate);
        ChannelState() = delete;

        ~ChannelState() = default;

        /** @brief Specify the next slot to modulate.
         * @param slot The new slot
         * @param overfill true if this slot can be overfilled
         */
        void nextSlot(const Slot *prev_slot, Slot &slot, const bool overfill);

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param g Soft (multiplicative) gain to apply to modulated signal.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        void modulate(std::shared_ptr<NetPacket> pkt,
                      const float g,
                      ModPacket &mpkt);

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

        /** @brief Number of valid samples in the frequency-domain buffer */
        /** This will be a multiple of N */
        size_t fdnsamples;

    protected:
        /** @brief Channel we are modulating */
        const Channel channel_;

        /** @brief Resampling rate */
        const double rate_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::Modulator> mod_;
    };

    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Flag indicating if we should stop processing packets */
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
    spinlock_mutex mods_mutex_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<ChannelState>> mods_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Synthesizer::Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief OLS time domain converter */
    Upsampler::ToTimeDomain timedomain_;

    /** @brief TX sample rate */
    double tx_rate_copy_;

    /** @brief Radio channels */
    Channels channels_copy_;

    /** @brief Radio schedule */
    Schedule schedule_copy_;

    /** @brief Number of channels available in each slot index */
    std::vector<unsigned> channel_count_;

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);
};

#endif /* FDCHANSYNTHESIZER_H_ */
