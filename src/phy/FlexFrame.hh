#ifndef FLEXFRAME_H_
#define FLEXFRAME_H_

#include <liquid/liquid.h>

#include "Liquid.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class FlexFrame : public PHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class Modulator : public PHY::Modulator
    {
    public:
        Modulator(FlexFrame& phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        /** @brief Print internals of the associated flexframegen. */
        void print(void);

        void modulate(ModPacket& mpkt, std::unique_ptr<NetPacket> pkt) override;

    private:
        /** @brief Associated FlexFrame PHY. */
        FlexFrame& _phy;

        /** @brief The liquid-dsp flexframegen object */
        flexframegen _fg;

        /** @brief The liquid-dsp flexframegenprops object  associated with this
          * flexframegen.
          */
        flexframegenprops_s _fgprops;

        /** Update frame properties to match _fgprops. */
        void update_props(NetPacket& pkt);
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidDemodulator
    {
    public:
        Demodulator(FlexFrame& phy);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        /** @brief Print internals of the associated flexframesync. */
        void print(void);

        void reset(Clock::time_point timestamp, size_t off) override;

        void demodulate(std::complex<float>* data,
                        size_t count,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback) override;

    private:
        /** @brief Associated FlexFrame PHY. */
        FlexFrame& _phy;

        /** @brief The liquid-dsp flexframesync object */
        flexframesync _fs;
    };

    FlexFrame(std::shared_ptr<Net> net,
              size_t minPacketSize) :
        _net(net),
        _minPacketSize(minPacketSize)
    {
    }

    ~FlexFrame()
    {
    }

    double getRxRateOversample(void) const override
    {
        return 2.0;
    }

    double getTxRateOversample(void) const override
    {
        return 2.0;
    }

    std::unique_ptr<PHY::Demodulator> make_demodulator(void) override;

    std::unique_ptr<PHY::Modulator> make_modulator(void) override;

private:
    /** @brief The Net to which we should send received packets. */
    std::shared_ptr<Net> _net;

    /** @brief Minimum packet size. */
    /** Packets will be padded to at least this many bytes */
    size_t _minPacketSize;
};

#endif /* FLEXFRAME_H_ */
