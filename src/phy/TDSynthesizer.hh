#ifndef TDSYNTHESIZER_H_
#define TDSYNTHESIZER_H_

#include <atomic>
#include <mutex>

#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "net/Net.hh"

/** @brief A time-domain synthesizer. */
class TDSynthesizer : public Synthesizer
{
public:
    TDSynthesizer(std::shared_ptr<PHY> phy,
                  double tx_rate,
                  const Channels &channels,
                  size_t nthreads);
    virtual ~TDSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void reconfigure(void) override;

    /** @brief Stop modulating. */
    void stop(void);

private:
    /** @brief Channel state for time-domain modulation */
    class ChannelState {
    public:
        using C = std::complex<float>;

        ChannelState(PHY &phy,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double tx_rate);

        ~ChannelState() = default;

        /** @brief Reset internal state */
        void reset(void);

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        void modulate(std::shared_ptr<NetPacket> pkt,
                      ModPacket &mpkt);

    protected:
        /** @brief Channel we are modulating */
        Channel channel_;

        /** @brief Resampling rate */
        double rate_;

        /** @brief Frequency shift in radians, i.e., 2*M_PI*shift/Fs */
        double rad_;

        /** @brief Resampler */
        Dragon::MixingRationalResampler<C,C> resamp_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::Modulator> mod_;
    };

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> mod_reconfigure_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Synthesizer::Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig, unsigned tid);
};

#endif /* TDSYNTHESIZER_H_ */
