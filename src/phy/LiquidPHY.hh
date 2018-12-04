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
                      ModPacket &mpkt) override final;

    protected:
        /** Our Liquid PHY */
        LiquidPHY &liquid_phy_;

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
                   size_t off,
                   double shift,
                   double rate) override final;

        void setSnapshotOffset(ssize_t snapshot_off) override final;

        void demodulate(const std::complex<float>* data,
                        size_t count,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback) override final;

    protected:
        /** @brief Our Liquid PHY */
        LiquidPHY &liquid_phy_;

        /** @brief Callback for received packets. */
        std::function<void(std::unique_ptr<RadioPacket>)> callback_;

        /** @brief Internal resampling factor. */
        /** This is the factor by which the PHY internally oversamples, i.e., the
         * samples seen by the Liquid demodulator are decimated by this amount. We
         * need this quantity in order to properly track demod_off_ and friends.
         */
        unsigned int internal_oversample_fact_;

        /** @brief Frequency shift of demodulated data */
        double shift_;

        /** @brief Resampler rate */
        /** This is used internally purely to properly timestamp packets.
         */
        double rate_;

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

        virtual int callback(unsigned char    *header_,
                             int              header_valid_,
                             unsigned char    *payload_,
                             unsigned int     payload_len_,
                             int              payload_valid_,
                             framesyncstats_s stats_) override;

        using Liquid::Demodulator::reset;

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
