#ifndef OFDM_H_
#define OFDM_H_

#include "liquid/OFDM.hh"
#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp ofdmflexframegen code. */
class OFDM : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp ofdmflexframegen. */
    class PacketModulator : public LiquidPHY::PacketModulator, protected Liquid::OFDMModulator
    {
    public:
        PacketModulator(OFDM &phy)
          : Liquid::Modulator(phy.header_mcs_)
          , LiquidPHY::PacketModulator(phy,
                                       phy.header_mcs_)
          , Liquid::OFDMModulator(phy.header_mcs_,
                                  phy.M_,
                                  phy.cp_len_,
                                  phy.taper_len_,
                                  phy.p_)
        {
        }

        virtual ~PacketModulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class PacketDemodulator : public LiquidPHY::PacketDemodulator, protected Liquid::OFDMDemodulator
    {
    public:
        PacketDemodulator(OFDM &phy)
          : Liquid::Demodulator(phy.header_mcs_,
                                phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::PacketDemodulator(phy,
                                         phy.header_mcs_,
                                         phy.soft_header_,
                                         phy.soft_payload_)
          , Liquid::OFDMDemodulator(phy.header_mcs_,
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
            return Liquid::OFDMDemodulator::isFrameOpen();
        }
    };

    /** @brief Construct an OFDM PHY.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     */
    OFDM(std::shared_ptr<SnapshotCollector> collector,
         NodeId node_id,
         const MCS &header_mcs,
         bool soft_header,
         bool soft_payload,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         const std::optional<std::string> &p)
      : LiquidPHY(collector, node_id, header_mcs, soft_header, soft_payload)
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

    Liquid::OFDMSubcarriers getSubcarriers(void) const
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
    Liquid::OFDMSubcarriers p_;

    std::shared_ptr<PHY::PacketDemodulator> mkPacketDemodulator(void) override
    {
        return std::make_shared<PacketDemodulator>(*this);
    }

    std::shared_ptr<PHY::PacketModulator> mkPacketModulator(void) override
    {
        return std::make_shared<PacketModulator>(*this);
    }

    std::unique_ptr<Liquid::Modulator> mkLiquidModulator(void) override
    {
        return std::make_unique<Liquid::OFDMModulator>(header_mcs_, M_, cp_len_, taper_len_, p_);
    }
};

#endif /* OFDM_H_ */
