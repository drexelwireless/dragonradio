#ifndef LIQUIDPHY_H_
#define LIQUIDPHY_H_

#include <complex>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Packet.hh"
#include "dsp/TableNCO.hh"
#include "liquid/Resample.hh"
#include "phy/PHY.hh"

struct ResamplerParams {
    ResamplerParams(void)
      : m(7)
      , fc(0.4f)
      , As(60.0f)
      , npfb(64)
    {
    }

    ~ResamplerParams() = default;

    /** @brief Prototype filter semi-length */
    unsigned int m;

    /** @brief Prototype filter cutoff frequency */
    float fc;

    /** @brief Stop-band attenuation for resamplers */
    float As;

    /** @brief Number of filters in polyphase filterbank */
    unsigned npfb;
};

class LiquidPHY : public PHY {
public:
    class Modulator : public PHY::Modulator {
    public:
        Modulator(LiquidPHY &phy);
        virtual ~Modulator();

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

        /** @brief Assemble a packet for modulation.
         * @param hdr Packet header
         * @param pkt The NetPacket to assemble.
         */
        virtual void assemble(unsigned char *hdr, NetPacket& pkt) = 0;

        /** @brief Return maximum number of samples modulateSamples will generate.
         * @return Maximum number of samples modulateSamples will generate.
         */
        virtual size_t maxModulatedSamples(void) = 0;

        /** @brief Modulate samples.
         * @param buf The destination for modulated samples
         * @param nw The number of samples written
         * @return A flag indicating whether or not the last sample was written.
         */
        virtual bool modulateSamples(std::complex<float> *buf, size_t &nw) = 0;
    };

    class Demodulator : public PHY::Demodulator {
    public:
        Demodulator(LiquidPHY &phy);
        virtual ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&&) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        void reset(Clock::time_point timestamp, size_t off) override final;

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

        /** @brief Frequency for mixing down */
        double shift_;

        /** @brief NCO for mixing down */
        TableNCO nco_;

        static int liquid_callback(unsigned char *  header_,
                                   int              header_valid_,
                                   unsigned char *  payload_,
                                   unsigned int     payload_len_,
                                   int              payload_valid_,
                                   framesyncstats_s stats_,
                                   void *           userdata_);

        virtual int callback(unsigned char *  header_,
                             int              header_valid_,
                             unsigned char *  payload_,
                             unsigned int     payload_len_,
                             int              payload_valid_,
                             framesyncstats_s stats_);

        /** @brief Set frequency shift for mixing down
         * @param shift The frequency shift (Hz)
         */
        virtual void setFreqShift(double shift);

        /** @brief Reset the internal state of the liquid demodulator. */
        virtual void liquidReset(void) = 0;

        /** @brief Demodulate samples.
         * @param buf The samples to demodulate
         * @param n The number of samples to demodulate
         */
        virtual void demodulateSamples(std::complex<float> *buf, const size_t n) = 0;
    };

    LiquidPHY(NodeId node_id,
              const MCS &header_mcs,
              bool soft_header,
              bool soft_payload,
              size_t min_packet_size);
    virtual ~LiquidPHY();

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

    /** @brief Get minimum packet size. */
    size_t getMinPacketSize() const
    {
        return min_packet_size_;
    }

    /** @brief Set minimum packet size. */
    void setMinPacketSize(size_t size)
    {
        min_packet_size_ = size;
    }

    /** @brief Resampler parameters for modulator */
    ResamplerParams upsamp_resamp_params;

    /** @brief Resampler parameters for demodulator */
    ResamplerParams downsamp_resamp_params;

protected:
    /** @brief Modulation and coding scheme for headers. */
    const MCS header_mcs_;

    /** @brief Flag indicating whether or not to use soft-decoding for headers.
      */
    const bool soft_header_;

    /** @brief Flag indicating whether or not to use soft-decoding for payload.
      */
    const bool soft_payload_;

    /** @brief Minimum packet size. */
    /** Packets will be padded to at least this many bytes */
    size_t min_packet_size_;
};

#endif /* LIQUIDPHY_H_ */
