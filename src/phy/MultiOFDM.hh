#ifndef MULTIOFDM_H_
#define MULTIOFDM_H_

#include <vector>
#include <complex>

#include <liquid/multichannelrx.h>
#include <liquid/multichanneltx.h>

#include "phy/Liquid.hh"

/** @brief A %PHY thats uses the liquid-usrp multi-channel OFDM %PHY code. */
class MultiOFDM : public LiquidPHY
{
public:
    /** @brief Modulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Modulator : public LiquidModulator
    {
    public:
        Modulator(MultiOFDM &phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM &myphy_;

        /** @brief Our liquid-usrp multichanneltx object. */
        std::unique_ptr<multichanneltx> mctx_;

        void assemble(unsigned char *hdr, NetPacket& pkt) override final;

        size_t maxModulatedSamples(void) override final;

        bool modulateSamples(std::complex<float> *buf, size_t &nw) override final;
    };

    /** @brief Demodulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Demodulator : public LiquidDemodulator
    {
    public:
        Demodulator(MultiOFDM &phy);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM &myphy_;

        /** @brief Our liquid-usrp multichannelrx object. */
        std::unique_ptr<multichannelrx> mcrx_;

        void liquidReset(void) override final;

        void demodulateSamples(std::complex<float> *buf, const size_t n) override final;
    };

    /** @brief Construct a multichannel OFDM PHY.
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     */
    MultiOFDM(const MCS &mcs,
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

    /** @brief Construct a multichannel OFDM PHY.
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     */
    MultiOFDM(const MCS &mcs,
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

    ~MultiOFDM()
    {
    }

    MultiOFDM(const MultiOFDM&) = delete;
    MultiOFDM(MultiOFDM&&) = delete;

    MultiOFDM& operator=(const MultiOFDM&) = delete;
    MultiOFDM& operator=(MultiOFDM&&) = delete;

    // MultiChannel TX/RX requires oversampling by a factor of 2
    double getMinRXRateOversample(void) const override
    {
        return 2.0;
    }

    double getMinTXRateOversample(void) const override
    {
        return 2.0;
    }

    size_t modulated_size(const TXParams &params, size_t n) override;

    std::unique_ptr<PHY::Demodulator> make_demodulator(void) override;

    std::unique_ptr<PHY::Modulator> make_modulator(void) override;

private:
    // OFDM parameters
    unsigned int M_;
    unsigned int cp_len_;
    unsigned int taper_len_;
    unsigned char *p_;
};

#endif /* MULTIOFDM_H_ */
