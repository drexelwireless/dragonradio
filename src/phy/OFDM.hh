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
        }

        virtual ~Demodulator() = default;

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
    OFDM(NodeId node_id,
         const MCS &header_mcs,
         bool soft_header,
         bool soft_payload,
         size_t min_packet_size,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         const std::vector<unsigned char> &p = {})
      : LiquidPHY(node_id, header_mcs, soft_header, soft_payload, min_packet_size)
      , M_(M)
      , cp_len_(cp_len)
      , taper_len_(taper_len)
      , p_(p)
    {
    }

    virtual ~OFDM() = default;

    double getMinRXRateOversample(void) const override
    {
        return 1.0;
    }

    double getMinTXRateOversample(void) const override
    {
        return 1.0;
    }

    std::unique_ptr<PHY::Demodulator> mkDemodulator(void) override
    {
        return std::make_unique<Demodulator>(*this);
    }

    std::unique_ptr<PHY::Modulator> mkModulator(void) override
    {
        return std::make_unique<Modulator>(*this);
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
    std::vector<unsigned char> p_;

    std::unique_ptr<Liquid::Modulator> mkLiquidModulator(void) override
    {
        return std::make_unique<Liquid::OFDMModulator>(M_, cp_len_, taper_len_, p_);
    }
};

#endif /* OFDM_H_ */
