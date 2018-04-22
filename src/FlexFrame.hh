#ifndef FLEXFRAME_H_
#define FLEXFRAME_H_

#include <liquid/liquid.h>

#include "NET.hh"
#include "PHY.hh"

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class FlexFrame : public PHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class Modulator : public PHY::Modulator
    {
    public:
        /** @brief Construct Modulator with default check, FEC's, and modulation
         * scheme.
         */
        Modulator(size_t minPacketSize);

        /** @brief Construct a flexframegen with the given check, inner and
         * outer FEC's, and modulation schemed.
         */
        explicit Modulator(size_t minPacketSize,
                           crc_scheme check,
                           fec_scheme fec0,
                           fec_scheme fec1,
                           modulation_scheme ms);

        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        virtual void setSoftTXGain(float dB) override;

        /** @brief Print internals of the associated flexframegen. */
        void print(void);

        /** @brief Get the data validity check used by the flexframe. */
        crc_scheme get_check(void);

        /** @brief Set the data validity check used by the flexframe. */
        void set_check(crc_scheme check);

        /** @brief Get the inner FEC used by the flexframe. */
        fec_scheme get_fec0(void);

        /** @brief Set the inner FEC used by the flexframe. */
        void set_fec0(fec_scheme fec0);

        /** @brief Get the outer FEC used by the flexframe. */
        fec_scheme get_fec1(void);

        /** @brief Set the outer FEC used by the flexframe. */
        void set_fec1(fec_scheme fec1);

        /** @brief Get the modulation scheme used by the flexframe. */
        modulation_scheme get_mod_scheme(void);

        /** @brief Set the modulation scheme used by the flexframe. */
        void set_mod_scheme(modulation_scheme ms);

        std::unique_ptr<ModPacket> modulate(std::unique_ptr<NetPacket> pkt) override;

    private:
        /** @brief Minimum packet size. */
        /** Packets will be padded to at least this many bytes */
        size_t minPacketSize;

        /** @brief Soft TX gain */
        float _g;

        /** @brief The liquid-dsp flexframegen object */
        flexframegen _fg;

        /** @brief The liquid-dsp flexframegenprops object  associated with this
          * flexframegen.
          */
        flexframegenprops_s _fgprops;

        /** Update frame properties to match _fgprops. */
        void update_props(void);
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public PHY::Demodulator
    {
    public:
        Demodulator(std::shared_ptr<NET> net);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        /** @brief Print internals of the associated flexframesync. */
        void print(void);

        void demodulate(std::unique_ptr<IQQueue> buf, std::queue<std::unique_ptr<RadioPacket>>& q) override;

    private:
        /** @brief NET object to which we send received packets. */
        std::shared_ptr<NET> net;

        /** @brief Queue of demodulated packets. */
        /** This variable is used by the liquid packet demodulation callback to
         * store demodulated packets.
         */
        std::queue<std::unique_ptr<RadioPacket>>* pkts;

        /** @brief The liquid-dsp flexframesync object */
        flexframesync _fs;

        static int _callback(unsigned char *  _header,
                             int              _header_valid,
                             unsigned char *  _payload,
                             unsigned int     _payload_len,
                             int              _payload_valid,
                             framesyncstats_s _stats,
                             void *           _userdata,
                             liquid_float_complex* G,
                             liquid_float_complex* G_hat,
                             unsigned int M);

        void callback(unsigned char *  _header,
                      int              _header_valid,
                      unsigned char *  _payload,
                      unsigned int     _payload_len,
                      int              _payload_valid,
                      framesyncstats_s _stats,
                      liquid_float_complex* G,
                      liquid_float_complex* G_hat,
                      unsigned int M);
    };

    FlexFrame(std::shared_ptr<NET> net,
              double bandwidth,
              size_t minPacketSize) :
        PHY(bandwidth),
        net(net),
        minPacketSize(minPacketSize)
    {
    }

    ~FlexFrame()
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

    std::unique_ptr<PHY::Demodulator> make_demodulator(void) const override;

    std::unique_ptr<PHY::Modulator> make_modulator(void) const override;

private:
    std::shared_ptr<NET> net;
    size_t minPacketSize;
};

#endif /* FLEXFRAME_H_ */
