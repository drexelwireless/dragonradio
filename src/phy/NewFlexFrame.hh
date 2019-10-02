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
          : Liquid::Modulator(phy.header_mcs_)
          , LiquidPHY::Modulator(phy)
          , Liquid::NewFlexFrameModulator(phy.header_mcs_)
        {
        }

        virtual ~Modulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidPHY::Demodulator, protected Liquid::NewFlexFrameDemodulator
    {
    public:
        Demodulator(NewFlexFrame &phy)
          : Liquid::Demodulator(phy.header_mcs_,
                                phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::Demodulator(phy)
          , Liquid::NewFlexFrameDemodulator(phy.header_mcs_,
                                            phy.soft_header_,
                                            phy.soft_payload_)
        {
        }

        virtual ~Demodulator() = default;

        bool isFrameOpen(void) override final
        {
            return Liquid::NewFlexFrameDemodulator::isFrameOpen();
        }
    };

    NewFlexFrame(std::shared_ptr<SnapshotCollector> collector,
                 NodeId node_id,
                 const MCS &header_mcs,
                 bool soft_header,
                 bool soft_payload,
                 size_t min_packet_size)
      : LiquidPHY(collector, node_id, header_mcs, soft_header, soft_payload, min_packet_size)
    {
    }

    virtual ~NewFlexFrame() = default;

    unsigned getMinRXRateOversample(void) const override
    {
        return 2;
    }

    unsigned getMinTXRateOversample(void) const override
    {
        return 2;
    }

protected:
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
        return std::make_unique<Liquid::NewFlexFrameModulator>(header_mcs_);
    }
};

#endif /* NEWFLEXFRAME_H_ */
