#ifndef LIQUIDPHY_H_
#define LIQUIDPHY_H_

#include <complex>
#include <functional>
#include <type_traits>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Logger.hh"
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

        void reset(double shift) override final;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<size_t> snapshot_off,
                       size_t offset,
                       float rate) override final;

        void demodulate(const std::complex<float>* data,
                        size_t count,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback) override final;

    protected:
        /** @brief Our Liquid PHY */
        LiquidPHY &liquid_phy_;

        /** @brief Callback for received packets. */
        std::function<void(std::unique_ptr<RadioPacket>)> callback_;

        /** @brief Frequency shift of demodulated data */
        double shift_;

        /** @brief Rate conversion from samples to full RX rate */
        /** This is used internally purely to properly timestamp packets. */
        double resamp_rate_;

        /** @brief Internal resampling factor. */
        /** This is the factor by which the PHY internally oversamples, i.e., the
         * samples seen by the Liquid demodulator are decimated by this amount. We
         * need this quantity in order to properly track demod_off_ and friends.
         */
        unsigned int internal_oversample_fact_;

        /** @brief Timestamp of current slot. */
        MonoClock::time_point timestamp_;

        /** @brief Snapshot offset of current slot. */
        std::optional<size_t> snapshot_off_;

        /** @brief Sample offset ffset of first provided sample from slot. */
        size_t offset_;

        /** @brief The sample number of the sample at offset in current slot */
        unsigned sample_start_;

        /** @brief The sample number of the last sample in current slot */
        unsigned sample_end_;

        /** @brief The sample counter. */
        unsigned sample_;

        /** @brief A reference to the global logger */
        std::shared_ptr<Logger> logger_;

        virtual int callback(unsigned char    *header_,
                             int              header_valid_,
                             int              header_test_,
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
