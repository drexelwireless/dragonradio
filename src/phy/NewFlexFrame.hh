#ifndef NEWFLEXFRAME_H_
#define NEWFLEXFRAME_H_

#include "liquid/NewFlexFrame.hh"
#include "liquid/PHY.hh"

#if defined(DOXYGEN)
#define final
#endif /* defined(DOXYGEN) */

namespace Liquid {

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class NewFlexFrame : public Liquid::PHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class PacketModulator : public Liquid::PHY::PacketModulator, protected Liquid::NewFlexFrameModulator
    {
    public:
        PacketModulator(NewFlexFrame& phy)
          : Liquid::Modulator(phy.header_mcs_)
          , Liquid::PHY::PacketModulator(phy,
                                         phy.header_mcs_)
          , Liquid::NewFlexFrameModulator(phy.header_mcs_)
        {
        }

        virtual ~PacketModulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class PacketDemodulator : public Liquid::PHY::PacketDemodulator, protected Liquid::NewFlexFrameDemodulator
    {
    public:
        PacketDemodulator(NewFlexFrame &phy)
          : Liquid::Demodulator(phy.header_mcs_,
                                phy.soft_header_,
                                phy.soft_payload_)
          , Liquid::PHY::PacketDemodulator(phy,
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

    NewFlexFrame(const MCS &header_mcs,
                 const std::vector<std::pair<MCS, AutoGain>> &mcs_table,
                 bool soft_header,
                 bool soft_payload)
      : Liquid::PHY(header_mcs,
                    mcs_table,
                    soft_header,
                    soft_payload)
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
    std::shared_ptr<::PHY::PacketDemodulator> mkPacketDemodulator(void) override
    {
        return std::make_shared<PacketDemodulator>(*this);
    }

    std::shared_ptr<::PHY::PacketModulator> mkPacketModulator(void) override
    {
        return std::make_shared<PacketModulator>(*this);
    }

    std::unique_ptr<Liquid::Modulator> mkLiquidModulator(void) override
    {
        return std::make_unique<Liquid::NewFlexFrameModulator>(header_mcs_);
    }
};

}

#endif /* NEWFLEXFRAME_H_ */
