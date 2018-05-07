#ifndef OFDM_H_
#define OFDM_H_

#include <liquid/liquid.h>

#include "NET.hh"
#include "phy/PHY.hh"

/** @brief A %PHY thats uses the liquid-usrp ofdmflexframegen code. */
class OFDM : public PHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp ofdmflexframegen. */
    class Modulator : public PHY::Modulator
    {
    public:
        Modulator(OFDM& phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        /** @brief Print internals of the associated flexframegen. */
        void print(void);

        void modulate(ModPacket& mpkt, std::unique_ptr<NetPacket> pkt) override;

    private:
        /** @brief Associated OFDM PHY. */
        OFDM& _phy;

        /** @brief The liquid-dsp flexframegen object */
        ofdmflexframegen _fg;

        /** @brief The liquid-dsp ofdmflexframegenprops object associated with
         * this ofdmflexframegen.
         */
        ofdmflexframegenprops_s _fgprops;

        /** Update frame properties to match _fgprops. */
        void update_props(NetPacket& pkt);
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidDemodulator
    {
    public:
        Demodulator(OFDM& phy);
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
        /** @brief Associated OFDM PHY. */
        OFDM& _phy;

        /** @brief The liquid-dsp flexframesync object */
        ofdmflexframesync _fs;
    };

    /** @brief Construct an OFDM PHY.
     * @param net The NET to which we should send received packets.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     */
    OFDM(std::shared_ptr<NET> net,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         unsigned char *p,
         size_t minPacketSize) :
         _M(M),
         _cp_len(cp_len),
         _taper_len(taper_len),
         _p(p),
         _net(net),
         _minPacketSize(minPacketSize)
    {
    }

    ~OFDM()
    {
    }

    double getRxRateOversample(void) const override
    {
        return 1.0;
    }

    double getTxRateOversample(void) const override
    {
        return 1.0;
    }

    std::unique_ptr<PHY::Demodulator> make_demodulator(void) override;

    std::unique_ptr<PHY::Modulator> make_modulator(void) override;

private:
    // OFDM parameters
    unsigned int _M;
    unsigned int _cp_len;
    unsigned int _taper_len;
    unsigned char *_p;

    /** @brief The NET to which we should send received packets. */
    std::shared_ptr<NET> _net;

    /** @brief Minimum packet size. */
    /** Packets will be padded to at least this many bytes */
    size_t _minPacketSize;
};

#endif /* OFDM_H_ */
