#ifndef MULTIOFDM_H_
#define MULTIOFDM_H_

#include <vector>
#include <complex>

#include <liquid/multichannelrx.h>
#include <liquid/multichanneltx.h>

#include "phy/Liquid.hh"
#include "net/Net.hh"

/** @brief A %PHY thats uses the liquid-usrp multi-channel OFDM %PHY code. */
class MultiOFDM : public PHY
{
public:
    /** @brief Modulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Modulator : public PHY::Modulator
    {
    public:
        Modulator(MultiOFDM& phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        void modulate(ModPacket& mpkt, std::unique_ptr<NetPacket> pkt) override;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM& phy_;

        /** @brief Our liquid-usrp multichanneltx object. */
        std::unique_ptr<multichanneltx> mctx_;
    };

    /** @brief Demodulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Demodulator : public LiquidDemodulator
    {
    public:
        Demodulator(MultiOFDM& phy);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        void reset(Clock::time_point timestamp, size_t off) override;

        void demodulate(std::complex<float>* data,
                        size_t count,
                        std::function<void(std::unique_ptr<RadioPacket>)> callback) override;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM& phy_;

        /** @brief Our liquid-usrp multichannelrx object. */
        std::unique_ptr<multichannelrx> mcrx_;
    };

    /** @brief Construct a multichannel OFDM PHY.
     * @param net The Net to which we should send received packets.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     */
    MultiOFDM(std::shared_ptr<Net> net,
              unsigned int M,
              unsigned int cp_len,
              unsigned int taper_len,
              size_t minPacketSize) :
              M_(M),
              cp_len_(cp_len),
              taper_len_(taper_len),
              p_(NULL),
              net_(net),
              min_pkt_size_(minPacketSize)
    {
    }

    /** @brief Construct a multichannel OFDM PHY.
     * @param net The Net to which we should send received packets.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     */
    MultiOFDM(std::shared_ptr<Net> net,
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

    ~MultiOFDM()
    {
    }

    MultiOFDM(const MultiOFDM&) = delete;
    MultiOFDM(MultiOFDM&&) = delete;

    MultiOFDM& operator=(const MultiOFDM&) = delete;
    MultiOFDM& operator=(MultiOFDM&&) = delete;

    // MultiChannel TX/RX requires oversampling by a factor of 2
    double getRXRateOversample(void) const override
    {
        return 2;
    }

    double getTXRateOversample(void) const override
    {
        return 2;
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

#endif /* MULTIOFDM_H_ */
