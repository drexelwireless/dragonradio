// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef NEWFLEXFRAME_H_
#define NEWFLEXFRAME_H_

#include "liquid/NewFlexFrame.hh"
#include "liquid/PHY.hh"

namespace liquid {

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class NewFlexFrame : public liquid::PHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class PacketModulator : public liquid::PHY::PacketModulator, protected liquid::NewFlexFrameModulator
    {
    public:
        PacketModulator(NewFlexFrame& phy)
          : liquid::Modulator(phy.header_mcs_)
          , liquid::PHY::PacketModulator(phy,
                                         phy.header_mcs_)
          , liquid::NewFlexFrameModulator(phy.header_mcs_)
        {
        }

        virtual ~PacketModulator() = default;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class PacketDemodulator : public liquid::PHY::PacketDemodulator, protected liquid::NewFlexFrameDemodulator
    {
    public:
        PacketDemodulator(NewFlexFrame &phy,
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
          , liquid::NewFlexFrameDemodulator(phy.header_mcs_,
                                            phy.soft_header_,
                                            phy.soft_payload_)
        {
        }

        virtual ~PacketDemodulator() = default;

        bool isFrameOpen(void) override final
        {
            return liquid::NewFlexFrameDemodulator::isFrameOpen();
        }
    };

    NewFlexFrame(const MCS &header_mcs,
                 const std::vector<std::pair<MCS, AutoGain>> &mcs_table,
                 bool soft_header,
                 bool soft_payload)
      : liquid::PHY(header_mcs,
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
        return std::make_unique<liquid::NewFlexFrameModulator>(header_mcs_);
    }
};

}

#endif /* NEWFLEXFRAME_H_ */
