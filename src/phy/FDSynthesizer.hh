#ifndef FDSYNTHESIZER_H_
#define FDSYNTHESIZER_H_

#include <atomic>
#include <mutex>

#include "spinlock_mutex.hh"
#include "dsp/FDResample.hh"
#include "dsp/FFTW.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "net/Net.hh"

/** @brief A frequency-domain synthesizer. */
class FDSynthesizer : public Synthesizer
{
public:
    using C = std::complex<float>;

    /** @brief Filter length */
    /** We need two factors of 5 because we need to support 25MHz bandwidth.
     * The rest of the factors of 2 are for good measure.
     */
    static constexpr unsigned P = 25*64+1;

    /** @brief Overlap factor */
    static constexpr unsigned V = 8;

    using Upsampler = FDUpsampler<C,P,V>;

    static constexpr auto N = Upsampler::N;
    static constexpr auto L = Upsampler::L;
    static constexpr auto O = Upsampler::O;

    FDSynthesizer(std::shared_ptr<PHY> phy,
                  double tx_rate,
                  const Channels &channels,
                  size_t nthreads);
    virtual ~FDSynthesizer();

    void setTXRate(double rate) override
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        tx_rate_ = rate;
        reconfigure();
    }

    void setChannels(const Channels &channels) override
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        channels_ = channels;
        reconfigure();
    }

    void setSchedule(const Schedule::sched_type &schedule) override
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        schedule_ = schedule;
        reconfigure();
    }

    void modulate(const std::shared_ptr<Slot> &slot) override;

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

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        void modulate(std::shared_ptr<NetPacket> pkt,
                      ModPacket &mpkt);

    protected:
        /** @brief Channel we are modulating */
        const Channel channel_;

        /** @brief Resampling rate */
        const double rate_;

        /** @brief OLS time domain converter */
        Upsampler::ToTimeDomain timedomain_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::Modulator> mod_;
    };

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Mutex for synthesizer state. */
    spinlock_mutex mutex_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> mod_reconfigure_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Synthesizer::Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig, unsigned tid);
};

#endif /* FDSYNTHESIZER_H_ */
