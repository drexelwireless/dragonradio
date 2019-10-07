#ifndef FLEXFRAME_H_
#define FLEXFRAME_H_

#include "liquid/FlexFrame.hh"
#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class FlexFrame : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class PacketModulator : public LiquidPHY::PacketModulator, protected Liquid::FlexFrameModulator
    {
    public:
        PacketModulator(FlexFrame &phy)
          : Liquid::Modulator(phy.header_mcs_)
          , LiquidPHY::PacketModulator(phy, phy.header_mcs_)
          , Liquid::FlexFrameModulator(phy.header_mcs_)
        {
        }

        virtual ~PacketModulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class PacketDemodulator : public LiquidPHY::PacketDemodulator, protected Liquid::FlexFrameDemodulator
    {
    public:
        PacketDemodulator(FlexFrame &phy)
          : Liquid::Demodulator(phy.header_mcs_,
                                phy.soft_header_,
                                phy.soft_payload_)
          , LiquidPHY::PacketDemodulator(phy,
                                         phy.header_mcs_,
                                         phy.soft_header_,
                                         phy.soft_payload_)
          , Liquid::FlexFrameDemodulator(phy.header_mcs_,
                                         phy.soft_header_,
                                         phy.soft_payload_)
        {
        }

        virtual ~PacketDemodulator() = default;

        bool isFrameOpen(void) override final
        {
            return Liquid::FlexFrameDemodulator::isFrameOpen();
        }
    };

    FlexFrame(std::shared_ptr<SnapshotCollector> collector,
              NodeId node_id,
              const MCS &header_mcs,
              bool soft_header,
              bool soft_payload)
      : LiquidPHY(collector, node_id, header_mcs, soft_header, soft_payload)
    {
    }

    virtual ~FlexFrame() = default;

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
        return std::make_unique<Liquid::FlexFrameModulator>(header_mcs_);
    }
};

#endif /* FLEXFRAME_H_ */
