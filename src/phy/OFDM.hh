#ifndef OFDM_H_
#define OFDM_H_

#include "phy/Liquid.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

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
        OFDM& phy_;

        /** @brief The liquid-dsp flexframegen object */
        ofdmflexframegen fg_;

        /** @brief The liquid-dsp ofdmflexframegenprops object associated with
         * this ofdmflexframegen.
         */
        ofdmflexframegenprops_s fgprops_;

        /** Update frame properties to match fgprops_. */
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
        OFDM& phy_;

        /** @brief The liquid-dsp flexframesync object */
        ofdmflexframesync fs_;
    };

    /** @brief Construct an OFDM PHY.
     * @param net The Net to which we should send received packets.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     */
    OFDM(std::shared_ptr<Net> net,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         unsigned char *p,
         size_t minPacketSize) :
         M_(M),
         cp_len_(cp_len),
         taper_len_(taper_len),
         p_(p),
         net_(net),
         min_pkt_size_(minPacketSize)
    {
    }

    ~OFDM()
    {
    }

    OFDM(const OFDM&) = delete;
    OFDM(OFDM&&) = delete;

    OFDM& operator=(const OFDM&) = delete;
    OFDM& operator=(OFDM&&) = delete;

    double getRXRateOversample(void) const override
    {
        return 1.0;
    }

    double getTXRateOversample(void) const override
    {
        return 1.0;
    }

    std::unique_ptr<PHY::Demodulator> make_demodulator(void) override;

    std::unique_ptr<PHY::Modulator> make_modulator(void) override;

private:
    // OFDM parameters
    unsigned int M_;
    unsigned int cp_len_;
    unsigned int taper_len_;
    unsigned char *p_;

    /** @brief The Net to which we should send received packets. */
    std::shared_ptr<Net> net_;

    /** @brief Minimum packet size. */
    /** Packets will be padded to at least this many bytes */
    size_t min_pkt_size_;
};

#endif /* OFDM_H_ */
