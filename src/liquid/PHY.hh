// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef LIQUID_PHY_HH_
#define LIQUID_PHY_HH_

#include <complex>
#include <functional>
#include <type_traits>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Logger.hh"
#include "Packet.hh"
#include "dsp/TableNCO.hh"
#include "liquid/Modem.hh"
#include "liquid/Resample.hh"
#include "phy/PHY.hh"

namespace liquid {

class PHY : public ::PHY {
public:
    class PacketModulator : public ::PHY::PacketModulator, virtual protected liquid::Modulator {
    public:
        PacketModulator(PHY &phy, const MCS &header_mcs)
          : liquid::Modulator(header_mcs)
          , ::PHY::PacketModulator(phy)
        {
        }

        virtual ~PacketModulator() = default;

        PacketModulator(const PacketModulator&) = delete;
        PacketModulator(PacketModulator&&) = delete;

        PacketModulator& operator=(const PacketModulator&) = delete;
        PacketModulator& operator=(PacketModulator&&) = delete;

        void modulate(std::shared_ptr<NetPacket> pkt,
                      const float g,
                      ModPacket &mpkt) override final;
    };

    class PacketDemodulator : public ::PHY::PacketDemodulator, virtual protected liquid::Demodulator {
    public:
        using liquid::Demodulator::demodulate;

        PacketDemodulator(PHY &phy,
                          unsigned chanidx,
                          const Channel &channel,
                          const MCS &header_mcs,
                          bool soft_header,
                          bool soft_payload);
        virtual ~PacketDemodulator() = default;

        PacketDemodulator(const PacketDemodulator&) = delete;
        PacketDemodulator(PacketDemodulator&&) = delete;

        PacketDemodulator& operator=(const PacketDemodulator&) = delete;
        PacketDemodulator& operator=(PacketDemodulator&&) = delete;

        void reset(const Channel &channel) override final;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset,
                       size_t delay,
                       float rate,
                       float rx_rate) override final;

        void demodulate(const std::complex<float>* data,
                        size_t count) override final
        {
            demodulateSamples(data, count);
            sample_end_ += count;
        }

    protected:
        /** @brief Filter delay */
        ssize_t delay_;

        /** @brief Rate conversion from samples to full RX rate */
        /** This is used internally purely to properly timestamp packets. */
        double resamp_rate_;

        /** @brief RX rate (Hz) */
        double rx_rate_;

        /** @brief Internal resampling factor. */
        /** This is the factor by which the PHY internally oversamples, i.e., the
         * samples seen by the Liquid demodulator are decimated by this amount. We
         * need this quantity in order to properly track demod_off_ and friends.
         */
        unsigned int internal_oversample_fact_;

        /** @brief Timestamp of current slot. */
        MonoClock::time_point timestamp_;

        /** @brief Snapshot offset of current slot. */
        std::optional<ssize_t> snapshot_off_;

        /** @brief Sample offset of first provided sample from slot. */
        ssize_t offset_;

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

        using liquid::Demodulator::reset;
    };

    PHY(const MCS &header_mcs,
        const std::vector<std::pair<MCS, AutoGain>> &mcs_table,
        bool soft_header,
        bool soft_payload);
    virtual ~PHY() = default;

    PHY() = delete;
    PHY(const PHY&) = delete;
    PHY(PHY&&) = delete;

    PHY& operator=(const PHY&) = delete;
    PHY& operator=(PHY&&) = delete;

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

    size_t getModulatedSize(mcsidx_t mcsidx, size_t n) override;

protected:
    /** @brief Modulation and coding scheme for headers. */
    MCS header_mcs_;

    /** @brief MCS table */
    std::vector<liquid::MCS> mcs_table_;

    /** @brief Flag indicating whether or not to use soft-decoding for headers.
      */
    bool soft_header_;

    /** @brief Flag indicating whether or not to use soft-decoding for payload.
      */
    bool soft_payload_;

    /** @brief Create underlying liquid modulator object */
    virtual std::unique_ptr<liquid::Modulator> mkLiquidModulator(void) = 0;
};

}

#endif /* LIQUID_PHY_HH_ */
