#ifndef OFDM_H_
#define OFDM_H_

#include "liquid/OFDM.hh"
#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp ofdmflexframegen code. */
class OFDM : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp ofdmflexframegen. */
    class Modulator : public LiquidPHY::Modulator, protected Liquid::OFDMModulator
    {
    public:
        Modulator(OFDM &phy)
          : LiquidPHY::Modulator(phy)
          , Liquid::OFDMModulator(phy.M_,
                                  phy.cp_len_,
                                  phy.taper_len_,
                                  phy.p_)
          , myphy_(phy)
        {
            setHeaderMCS(phy.header_mcs_);
        }

        virtual ~Modulator() = default;

    private:
        /** @brief Our associated PHY. */
        OFDM &myphy_;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidPHY::Demodulator, protected Liquid::OFDMDemodulator
    {
    public:
        Demodulator(OFDM &phy)
          : Liquid::Demodulator(phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::Demodulator(phy)
          , Liquid::OFDMDemodulator(phy.soft_header_,
                                    phy.soft_payload_,
                                    phy.M_,
                                    phy.cp_len_,
                                    phy.taper_len_,
                                    phy.p_)
          , myphy_(phy)
        {
            setHeaderMCS(phy.header_mcs_);
        }

        virtual ~Demodulator() = default;

        bool isFrameOpen(void) override final
        {
            return Liquid::OFDMDemodulator::isFrameOpen();
        }

    private:
        /** @brief Our associated PHY. */
        OFDM &myphy_;
    };

    /** @brief Construct an OFDM PHY.
     * @param min_packet_size The minimum number of bytes we will send in a
     * packet.
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
         size_t min_packet_size,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         const std::optional<std::string> &p)
      : LiquidPHY(collector, node_id, header_mcs, soft_header, soft_payload, min_packet_size)
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

    std::shared_ptr<PHY::Demodulator> mkDemodulatorInternal(void) override
    {
        return std::make_shared<Demodulator>(*this);
    }

    std::shared_ptr<PHY::Modulator> mkModulatorInternal(void) override
    {
        return std::make_shared<Modulator>(*this);
    }

    std::unique_ptr<Liquid::Modulator> mkLiquidModulator(void) override
    {
        return std::make_unique<Liquid::OFDMModulator>(M_, cp_len_, taper_len_, p_);
    }
};

#endif /* OFDM_H_ */
