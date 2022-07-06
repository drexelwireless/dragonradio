// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef OFDM_H_
#define OFDM_H_

#include "liquid/OFDM.hh"
#include "liquid/PHY.hh"

namespace liquid {

/** @brief A %PHY thats uses the liquid-usrp ofdmflexframegen code. */
class OFDM : public liquid::PHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp ofdmflexframegen. */
    class PacketModulator : public liquid::PHY::PacketModulator, protected liquid::OFDMModulator
    {
    public:
        PacketModulator(OFDM &phy)
          : liquid::Modulator(phy.header_mcs_)
          , liquid::PHY::PacketModulator(phy,
                                         phy.header_mcs_)
          , liquid::OFDMModulator(phy.header_mcs_,
                                  phy.M_,
                                  phy.cp_len_,
                                  phy.taper_len_,
                                  phy.p_)
        {
        }

        virtual ~PacketModulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class PacketDemodulator : public liquid::PHY::PacketDemodulator, protected liquid::OFDMDemodulator
    {
    public:
        PacketDemodulator(OFDM &phy,
                          unsigned chanidx,
                          const Channel &channel)
          : liquid::Demodulator(phy.header_mcs_,
                                phy.soft_header_,
                                phy.soft_payload_)
          , liquid::PHY::PacketDemodulator(phy,
                                           chanidx,
                                           channel,
                                           phy.header_mcs_,
                                           phy.soft_header_,
                                           phy.soft_payload_)
          , liquid::OFDMDemodulator(phy.header_mcs_,
                                    phy.soft_header_,
                                    phy.soft_payload_,
                                    phy.M_,
                                    phy.cp_len_,
                                    phy.taper_len_,
                                    phy.p_)
        {
        }

        virtual ~PacketDemodulator() = default;

        bool isFrameOpen(void) override final
        {
            return liquid::OFDMDemodulator::isFrameOpen();
        }
    };

    /** @brief Construct an OFDM PHY.
     * @param header_mcs The MCS used to the packet header
     * @param mcs_table The MCS table
     * @param soft_header True if soft decoding should be used for header
     * @param soft_payload True if soft decoding should be used for payload
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     */
    OFDM(const MCS &header_mcs,
         const std::vector<std::pair<MCS, AutoGain>> &mcs_table,
         bool soft_header,
         bool soft_payload,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         const std::optional<std::string> &p)
     : liquid::PHY(header_mcs,
                   mcs_table,
                   soft_header,
                   soft_payload)
      , M_(M)
      , cp_len_(cp_len)
      , taper_len_(taper_len)
      , p_(M)
    {
        if (p) {
            if (p->size() != M) {
                std::stringstream buffer;

                buffer << "Subcarrier allocation must have " << M
                       << "elements but got" << p->size();

                throw std::range_error(buffer.str());
            }

            p_ = *p;
        }
    }

    virtual ~OFDM() = default;

    unsigned getMinRXRateOversample(void) const override
    {
        return 1;
    }

    unsigned getMinTXRateOversample(void) const override
    {
        return 1;
    }

    liquid::OFDMSubcarriers getSubcarriers(void) const
    {
        return p_;
    }

protected:
    /** @brief The number of subcarriers */
    unsigned int M_;

    /** @brief The cyclic prefix length */
    unsigned int cp_len_;

    /** @brief The taper length (OFDM symbol overlap) */
    unsigned int taper_len_;

    /** @brief The subcarrier allocation (null, pilot, data). Should have M
     * entries.
     */
    liquid::OFDMSubcarriers p_;

    std::shared_ptr<::PHY::PacketDemodulator> mkPacketDemodulator(unsigned chanidx, const Channel &channel) override
    {
        return std::make_shared<PacketDemodulator>(*this, chanidx, channel);
    }

    std::shared_ptr<::PHY::PacketModulator> mkPacketModulator(void) override
    {
        return std::make_shared<PacketModulator>(*this);
    }

    std::unique_ptr<liquid::Modulator> mkLiquidModulator(void) override
    {
        return std::make_unique<liquid::OFDMModulator>(header_mcs_, M_, cp_len_, taper_len_, p_);
    }
};

}

#endif /* OFDM_H_ */
