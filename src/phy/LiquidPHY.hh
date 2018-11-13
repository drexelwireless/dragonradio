#ifndef LIQUIDPHY_H_
#define LIQUIDPHY_H_

#include <complex>
#include <functional>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Packet.hh"
#include "dsp/TableNCO.hh"
#include "liquid/PHY.hh"
#include "liquid/Resample.hh"
#include "mac/Snapshot.hh"
#include "phy/PHY.hh"

struct ResamplerParams {
    using update_t = std::function<void(void)>;

    ResamplerParams(update_t update)
      : m(7)
      , fc(0.4f)
      , As(60.0f)
      , npfb(64)
      , update_(update)
    {
    }

    ~ResamplerParams() = default;

    ResamplerParams() = delete;

    unsigned get_m(void)
    {
        return m;
    }

    void set_m(unsigned m_new)
    {
        m = m_new;
        update_();
    }

    float get_fc(void)
    {
        return fc;
    }

    void set_fc(float fc_new)
    {
        fc = fc_new;
        update_();
    }

    float get_As(void)
    {
        return fc;
    }

    void set_As(float As_new)
    {
        As = As_new;
        update_();
    }

    unsigned get_npfb(void)
    {
        return npfb;
    }

    void set_npfb(unsigned npfb_new)
    {
        npfb = npfb_new;
        update_();
    }

    /** @brief Prototype filter semi-length */
    unsigned int m;

    /** @brief Prototype filter cutoff frequency */
    float fc;

    /** @brief Stop-band attenuation for resamplers */
    float As;

    /** @brief Number of filters in polyphase filterbank */
    unsigned npfb;

protected:
    /** @brief Callback called when variables are modified via set* */
    update_t update_;
};

class LiquidPHY : public PHY {
public:
    class Modulator : public PHY::Modulator, virtual protected Liquid::Modulator {
    public:
        Modulator(LiquidPHY &phy);
        virtual ~Modulator() = default;

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&&) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        void modulate(std::shared_ptr<NetPacket> pkt,
                      double shift,
                      ModPacket &mpkt) override final;

    protected:
        /** Our Liquid PHY */
        LiquidPHY &liquid_phy_;

        /** @brief Upsampler. */
        Liquid::MultiStageResampler upsamp_;

        /** @brief Frequency for mixing up */
        double shift_;

        /** @brief NCO for mixing up */
        TableNCO nco_;

        /** @brief Set frequency shift for mixing up
         * @param shift The frequency shift (Hz)
         */
        virtual void setFreqShift(double shift);

        virtual void reconfigure(void) override;
    };

    class Demodulator : public PHY::Demodulator, virtual protected Liquid::Demodulator {
    public:
        Demodulator(LiquidPHY &phy);
        virtual ~Demodulator() = default;

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&&) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        void reset(Clock::time_point timestamp,
                   size_t off) override final;

        void setSnapshotOffset(ssize_t snapshot_off) override final;

        void demodulate(std::complex<float>* data,
                        size_t count,
                        double shift,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback) override final;

    protected:
        /** Our Liquid PHY */
        LiquidPHY &liquid_phy_;

        /** @brief Downsampler. */
        Liquid::MultiStageResampler downsamp_;

        /** @brief Callback for received packets. */
        std::function<void(std::unique_ptr<RadioPacket>)> callback_;

        /** @brief Internal resampling factor. */
        /** This is the factor by which the PHY internally oversamples, i.e., the
         * samples seen by the Liquid demodulator are decimated by this amount. We
         * need this quantity in order to properly track demod_off_ and friends.
         */
        unsigned int internal_oversample_fact_;

        /** @brief The timestamp of the slot we are demodulating. */
        Clock::time_point demod_start_;

        /** @brief The offset (in samples) from the beggining of the slot at
         * which we started demodulating.
         */
        size_t demod_off_;

        /** @brief Are we snapshotting? */
        bool in_snapshot_;

        /** @brief The snapshot offset. */
        size_t snapshot_off_;

        /** @brief Frequency for mixing down */
        double shift_;

        /** @brief NCO for mixing down */
        TableNCO nco_;

        virtual int callback(unsigned char    *header_,
                             int              header_valid_,
                             unsigned char    *payload_,
                             unsigned int     payload_len_,
                             int              payload_valid_,
                             framesyncstats_s stats_) override;

        using Liquid::Demodulator::reset;

        /** @brief Set frequency shift for mixing down
         * @param shift The frequency shift (Hz)
         */
        virtual void setFreqShift(double shift);

        virtual void reconfigure(void) override;
    };

    LiquidPHY(std::shared_ptr<SnapshotCollector> collector,
              NodeId node_id,
              const MCS &header_mcs,
              bool soft_header,
              bool soft_payload,
              size_t min_packet_size);
    virtual ~LiquidPHY() = default;

    LiquidPHY() = delete;
    LiquidPHY(const LiquidPHY&) = delete;
    LiquidPHY(LiquidPHY&&) = delete;

    LiquidPHY& operator=(const LiquidPHY&) = delete;
    LiquidPHY& operator=(LiquidPHY&&) = delete;

    /** @brief Return modulation and coding scheme used for headers. */
    const MCS &getHeaderMCS() const
    {
        return header_mcs_;
    }

    /** @brief Return flag indicating whether or not to use soft-decoding for
      * headers.
      */
    bool getSoftHeader() const
    {
        return soft_header_;
    }

    /** @brief Return flag indicating whether or not to use soft-decoding for
      * payload.
      */
    bool getSoftPayload() const
    {
        return soft_payload_;
    }

    /** @brief Return minimum packet size.
      */
    size_t getMinPacketSize() const
    {
        return min_packet_size_;
    }

    /** @brief Return flag indicating whether or not to use soft-decoding for
      * payload.
      */
    void setMinPacketSize(size_t size)
    {
        min_packet_size_ = size;
    }

    size_t getModulatedSize(const TXParams &params, size_t n) override;

    /** @brief Resampler parameters for modulator */
    ResamplerParams upsamp_resamp_params;

    /** @brief Resampler parameters for demodulator */
    ResamplerParams downsamp_resamp_params;

protected:
    /** @brief Our snapshot collector */
    std::shared_ptr<SnapshotCollector> snapshot_collector_;

    /** @brief Modulation and coding scheme for headers. */
    MCS header_mcs_;

    /** @brief Flag indicating whether or not to use soft-decoding for headers.
      */
    bool soft_header_;

    /** @brief Flag indicating whether or not to use soft-decoding for payload.
      */
    bool soft_payload_;

    /** @brief Minimum packet size. */
    /** Packets will be padded to at least this many bytes */
    size_t min_packet_size_;

    /** @brief Create underlying liquid modulator object */
    virtual std::unique_ptr<Liquid::Modulator> mkLiquidModulator(void) = 0;
};

#endif /* LIQUIDPHY_H_ */
