#ifndef NEWFLEXFRAME_H_
#define NEWFLEXFRAME_H_

#include "liquid/NewFlexFrame.hh"
#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class NewFlexFrame : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class Modulator : public LiquidPHY::Modulator, protected Liquid::NewFlexFrameModulator
    {
    public:
        Modulator(NewFlexFrame& phy)
          : LiquidPHY::Modulator(phy)
          , Liquid::NewFlexFrameModulator()
        {
        }

        virtual ~Modulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidPHY::Demodulator, protected Liquid::NewFlexFrameDemodulator
    {
    public:
        Demodulator(NewFlexFrame &phy)
          : Liquid::Demodulator(phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::Demodulator(phy)
          , Liquid::NewFlexFrameDemodulator(phy.soft_header_, phy.soft_payload_)
        {
        }

        virtual ~Demodulator() = default;
    };

    NewFlexFrame(NodeId node_id,
                 const MCS &header_mcs,
                 bool soft_header,
                 bool soft_payload,
                 size_t min_packet_size)
      : LiquidPHY(node_id, header_mcs, soft_header, soft_payload, min_packet_size)
    {
    }

    virtual ~NewFlexFrame() = default;

    double getMinRXRateOversample(void) const override
    {
        return 2.0;
    }

    double getMinTXRateOversample(void) const override
    {
        return 2.0;
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
    std::unique_ptr<Liquid::Modulator> mkLiquidModulator(void) override
    {
        return std::make_unique<Liquid::NewFlexFrameModulator>();
    }
};

#endif /* NEWFLEXFRAME_H_ */
