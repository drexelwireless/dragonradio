#ifndef MULTIOFDM_H_
#define MULTIOFDM_H_

#include <memory>

#include "liquid/MultiOFDM.hh"
#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp multi-channel OFDM %PHY code. */
class MultiOFDM : public LiquidPHY
{
public:
    /** @brief Modulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Modulator : public LiquidPHY::Modulator, protected Liquid::MultiOFDMModulator
    {
    public:
        Modulator(MultiOFDM &phy)
          : LiquidPHY::Modulator(phy)
          , Liquid::MultiOFDMModulator(phy.M_,
                                       phy.cp_len_,
                                       phy.taper_len_,
                                       phy.p_)
          , myphy_(phy)
        {
        }

        virtual ~Modulator() = default;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM &myphy_;
    };

    /** @brief Demodulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Demodulator : public LiquidPHY::Demodulator, protected Liquid::MultiOFDMDemodulator
    {
    public:
        Demodulator(MultiOFDM &phy)
          : Liquid::Demodulator(phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::Demodulator(phy)
          , Liquid::MultiOFDMDemodulator(phy.soft_header_,
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
        MultiOFDM &myphy_;
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
    MultiOFDM(std::shared_ptr<SnapshotCollector> collector,
              NodeId node_id,
              const MCS &header_mcs,
              bool soft_header,
              bool soft_payload,
              size_t min_packet_size,
              unsigned int M,
              unsigned int cp_len,
              unsigned int taper_len,
              const std::vector<unsigned char> &p = {})
       : LiquidPHY(collector, node_id, header_mcs, soft_header, soft_payload, min_packet_size)
       , M_(M)
       , cp_len_(cp_len)
       , taper_len_(taper_len)
       , p_(p)
    {
    }

    virtual ~MultiOFDM() = default;

    // MultiChannel TX/RX requires oversampling by a factor of 2
    unsigned getMinRXRateOversample(void) const override
    {
        return 2;
    }

    unsigned getMinTXRateOversample(void) const override
    {
        return 2;
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
        return std::make_unique<Liquid::MultiOFDMModulator>(M_, cp_len_, taper_len_, p_);
    }
};

#endif /* MULTIOFDM_H_ */
