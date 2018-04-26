#ifndef MULTIOFDM_H_
#define MULTIOFDM_H_

#include <vector>
#include <complex>

#include <liquid/liquid.h>
#include <liquid/multichannelrx.h>
#include <liquid/multichanneltx.h>

#include "ModPacket.hh"
#include "NET.hh"
#include "PHY.hh"

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
        /**
         * @param minPacketSize The minimum number of bytes we will send in a
         * packet.
         */
        Modulator(MultiOFDM& phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        virtual void setSoftTXGain(float dB) override;

        std::unique_ptr<ModPacket> modulate(std::unique_ptr<NetPacket> pkt) override;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM& _phy;

        /** @brief Soft TX gain. */
        float _g;

        /** @brief Our liquid-usrp multichanneltx object. */
        std::unique_ptr<multichanneltx> _mctx;
    };

    /** @brief Demodulate IQ data using the liquid-usrp multi-channel OFDM %PHY
      * code.
      */
    class Demodulator : public PHY::Demodulator
    {
    public:
        Demodulator(MultiOFDM& phy);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        void demodulate(std::unique_ptr<IQQueue> buf) override;

    private:
        /** @brief Our associated PHY. */
        MultiOFDM& _phy;

        /** @brief Our liquid-usrp multichannelrx object. */
        std::unique_ptr<multichannelrx> mcrx;

        static int liquidRxCallback(unsigned char *  _header,
                                    int              _header_valid,
                                    unsigned char *  _payload,
                                    unsigned int     _payload_len,
                                    int              _payload_valid,
                                    framesyncstats_s _stats,
                                    void *           _userdata,
                                    liquid_float_complex* G,
                                    liquid_float_complex* G_hat,
                                    unsigned int M);

        int rxCallback(unsigned char *  _header,
                       int              _header_valid,
                       unsigned char *  _payload,
                       unsigned int     _payload_len,
                       int              _payload_valid,
                       framesyncstats_s _stats,
                       liquid_float_complex* G,
                       liquid_float_complex* G_hat,
                       unsigned int M);
    };


    /**
     * @param net The NET to which we send demodulated packets.
     * @prama sink The RadioPacketSink to which we should send received packets.
     * @prama bandwidth The bandwidth used by the PHY (without oversampling).
     * @param minPacketSize The minimum number of bytes we will send in a
     * packet.
     */
    MultiOFDM(std::shared_ptr<RadioPacketSink> sink,
              double bandwidth,
              size_t minPacketSize) :
        PHY(bandwidth),
        _sink(sink),
        _minPacketSize(minPacketSize)
    {
    }

    // MultiChannel TX/RX requires oversampling by a factor of 2
    double getRxRateOversample(void) const override
    {
        return 2;
    }

    double getTxRateOversample(void) const override
    {
        return 2;
    }

    std::unique_ptr<PHY::Demodulator> make_demodulator(void) override
    {
        return std::unique_ptr<PHY::Demodulator>(static_cast<PHY::Demodulator*>(new Demodulator(*this)));
    }

    std::unique_ptr<PHY::Modulator> make_modulator(void) override
    {
        return std::unique_ptr<PHY::Modulator>(static_cast<PHY::Modulator*>(new Modulator(*this)));
    }

private:
    /** @brief The RadioPacketSink to which we should send received packets. */
    std::shared_ptr<RadioPacketSink> _sink;

    /** @brief Minimum packet size. */
    /** Packets will be padded to at least this many bytes */
    size_t _minPacketSize;
};

#endif /* MULTIOFDM_H_ */
