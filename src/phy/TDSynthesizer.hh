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
    TDSynthesizer(std::shared_ptr<Net> net,
                  std::shared_ptr<PHY> phy,
                  double tx_rate,
                  const Channel &tx_channel,
                  size_t nthreads);
    virtual ~TDSynthesizer();

    double getMaxTXUpsampleRate(void) override;

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void reconfigure(void) override;

    /** @brief Get TX channel. */
    Channel getTXChannel(void)
    {
        return tx_channel_;
    }

    /** @brief Set TX channel. */
    void setTXChannel(const Channel &channel)
    {
        tx_channel_ = channel;
        reconfigure();
    }

    /** @brief Get prototype filter for channelization. */
    const std::vector<C> &getTaps(void) const
    {
        return taps_;
    }

    /** @brief Set prototype filter for channelization. */
    /** The prototype filter should have unity gain. */
    void setTaps(const std::vector<C> &taps)
    {
        taps_ = taps;
        reconfigure();
    }

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

    /** @brief Our network. */
    std::shared_ptr<Net> net_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Prototype filter */
    std::vector<C> taps_;

    /** @brief TX channel */
    Channel tx_channel_;

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
