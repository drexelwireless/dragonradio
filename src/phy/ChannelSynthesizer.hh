#ifndef CHANNELSYNTHESIZER_HH_
#define CHANNELSYNTHESIZER_HH_

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

/** @brief A single-channel synthesizer. */
class ChannelSynthesizer : public Synthesizer
{
public:
    using C = std::complex<float>;

    ChannelSynthesizer(std::shared_ptr<PHY> phy,
                       double tx_rate,
                       const Channels &channels,
                       size_t nthreads);
    virtual ~ChannelSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void reconfigure(void) override;

    /** @brief Stop modulating. */
    void stop(void);

protected:
    /** @brief Channel state for time-domain modulation */
    class ChannelState {
    public:
        ChannelState(PHY &phy,
                     unsigned chanidx,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double tx_rate)
          : chanidx_(chanidx)
          , channel_(channel)
          // XXX Protected against channel with zero bandwidth
          , rate_(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw))
          , fshift_(channel.fc/tx_rate)
          , mod_(phy.mkPacketModulator())
        {
        }

        ChannelState() = delete;

        virtual ~ChannelState() = default;

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param g Gain to apply.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        virtual void modulate(std::shared_ptr<NetPacket> pkt,
                              float g,
                              ModPacket &mpkt) = 0;

    protected:
        /** @brief Index of channel we are modulating */
        const unsigned chanidx_;

        /** @brief Channel we are modulating */
        const Channel channel_;

        /** @brief Resampling rate */
        const double rate_;

        /** @brief Frequency shift */
        const double fshift_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::PacketModulator> mod_;
    };

    /** @brief Flag indicating if we should stop processing packets */
    std::atomic<bool> done_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> mod_reconfigure_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Synthesizer::Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Create a ChannelState */
    virtual std::unique_ptr<ChannelState> mkChannelState(unsigned chanidx,
                                                         const Channel &channel,
                                                         const std::vector<C> &taps,
                                                         double tx_rate) = 0;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig, unsigned tid);
};

#endif /* CHANNELSYNTHESIZER_HH_ */
