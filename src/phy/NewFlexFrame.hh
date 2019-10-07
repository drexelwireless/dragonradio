#ifndef NEWFLEXFRAME_H_
#define NEWFLEXFRAME_H_

#include "liquid/NewFlexFrame.hh"
#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class NewFlexFrame : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class PacketModulator : public LiquidPHY::PacketModulator, protected Liquid::NewFlexFrameModulator
    {
    public:
        PacketModulator(NewFlexFrame& phy)
          : Liquid::Modulator(phy.header_mcs_)
          , LiquidPHY::PacketModulator(phy,
                                       phy.header_mcs_)
          , Liquid::NewFlexFrameModulator(phy.header_mcs_)
        {
        }

        virtual ~PacketModulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class PacketDemodulator : public LiquidPHY::PacketDemodulator, protected Liquid::NewFlexFrameDemodulator
    {
    public:
        PacketDemodulator(NewFlexFrame &phy)
          : Liquid::Demodulator(phy.header_mcs_,
                                phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::PacketDemodulator(phy,
                                         phy.header_mcs_,
                                         phy.soft_header_,
                                         phy.soft_payload_)
          , Liquid::NewFlexFrameDemodulator(phy.header_mcs_,
                                            phy.soft_header_,
                                            phy.soft_payload_)
        {
        }

        virtual ~PacketDemodulator() = default;

        bool isFrameOpen(void) override final
        {
            return Liquid::NewFlexFrameDemodulator::isFrameOpen();
        }
    };

    NewFlexFrame(std::shared_ptr<SnapshotCollector> collector,
                 NodeId node_id,
                 const MCS &header_mcs,
                 bool soft_header,
                 bool soft_payload)
      : LiquidPHY(collector, node_id, header_mcs, soft_header, soft_payload)
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
        return std::make_unique<Liquid::NewFlexFrameModulator>(header_mcs_);
    }
};

#endif /* NEWFLEXFRAME_H_ */
