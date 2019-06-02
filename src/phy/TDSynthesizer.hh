#ifndef TDSYNTHESIZER_H_
#define TDSYNTHESIZER_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "net/Net.hh"

/** @brief A time-domain synthesizer. */
class TDSynthesizer : public Synthesizer, public Element
{
public:
    using C = std::complex<float>;

    TDSynthesizer(std::shared_ptr<Net> net,
                  std::shared_ptr<PHY> phy,
                  double tx_rate,
                  const Channel &tx_channel,
                  size_t nthreads);
    virtual ~TDSynthesizer();

    double getMaxTXUpsampleRate(void) override;

    virtual void modulateOne(std::shared_ptr<NetPacket> pkt,
                             ModPacket &mpkt) override;

    void modulate(size_t n) override;

    size_t pop(std::list<std::unique_ptr<ModPacket>>& pkts,
               size_t maxSamples,
               bool overfill) override;

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

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

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
         * @param channel The channel being modulated.
         * @param pkt The NetPacket to modulate.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        void modulate(const Channel &channel,
                      std::shared_ptr<NetPacket> pkt,
                      ModPacket &mpkt);

    protected:
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

    /** @brief Thread running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Number of modulated samples we want. */
    size_t nwanted_;

    /** @brief Number of modulated samples we have */
    size_t nsamples_;

    /** @brief Mutex to serialize access to the network */
    std::mutex net_mutex_;

    /* @brief Mutex protecting queue of modulated packets */
    std::mutex pkt_mutex_;

    /* @brief Condition variable used to signal modulation workers */
    std::condition_variable producer_cond_;

    /* @brief Queue of modulated packets */
    std::list<std::unique_ptr<ModPacket>> pkt_q_;

    /* @brief Modulator for one-off modulation */
    std::unique_ptr<ChannelState> one_mod_;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig);
};

#endif /* TDSYNTHESIZER_H_ */
