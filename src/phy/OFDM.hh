#ifndef OFDM_H_
#define OFDM_H_

#include "phy/Liquid.hh"

/** @brief A %PHY thats uses the liquid-usrp ofdmflexframegen code. */
class OFDM : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp ofdmflexframegen. */
    class Modulator : public LiquidModulator
    {
    public:
        Modulator(OFDM &phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        /** @brief Print internals of the associated flexframegen. */
        void print(void);

    private:
        /** @brief Associated OFDM PHY. */
        OFDM &myphy_;

        /** @brief The liquid-dsp flexframegen object */
        ofdmflexframegen fg_;

        /** @brief The liquid-dsp ofdmflexframegenprops object associated with
         * this ofdmflexframegen.
         */
        ofdmflexframegenprops_s fgprops_;

        /** Update frame properties to match fgprops_. */
        void update_props(const TXParams &params);

        void assemble(unsigned char *hdr, NetPacket& pkt) override final;

        size_t maxModulatedSamples(void) override final;
        
        bool modulateSamples(std::complex<float> *buf, size_t &nw) override final;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidDemodulator
    {
    public:
        Demodulator(OFDM &phy);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        /** @brief Print internals of the associated flexframesync. */
        void print(void);

    private:
        /** @brief Associated OFDM PHY. */
        OFDM &myphy_;

        /** @brief The liquid-dsp flexframesync object */
        ofdmflexframesync fs_;

        void liquidReset(void) override final;

        void demodulateSamples(std::complex<float> *buf, const size_t n) override final;
    };

    /** @brief Construct an OFDM PHY.
     * @param min_packet_size The minimum number of bytes we will send in a
     * packet.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     */
    OFDM(const MCS &mcs,
         bool soft_header,
         bool soft_payload,
         size_t min_packet_size,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len)
      : LiquidPHY(mcs, soft_header, soft_payload, min_packet_size)
      , M_(M)
      , cp_len_(cp_len)
      , taper_len_(taper_len)
      , p_(NULL)
    {
    }

    /** @brief Construct an OFDM PHY.
     * @param min_packet_size The minimum number of bytes we will send in a
     * packet.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     */
    OFDM(const MCS &mcs,
         bool soft_header,
         bool soft_payload,
         size_t min_packet_size,
         unsigned int M,
         unsigned int cp_len,
         unsigned int taper_len,
         unsigned char *p)
      : LiquidPHY(mcs, soft_header, soft_payload, min_packet_size)
      , M_(M)
      , cp_len_(cp_len)
      , taper_len_(taper_len)
      , p_(p)
    {
    }

    ~OFDM()
    {
    }

    OFDM(const OFDM&) = delete;
    OFDM(OFDM&&) = delete;

    OFDM& operator=(const OFDM&) = delete;
    OFDM& operator=(OFDM&&) = delete;

    double getMinRXRateOversample(void) const override
    {
        return 1.0;
    }

    double getMinTXRateOversample(void) const override
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
};

#endif /* OFDM_H_ */
