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
        /** @brief Construct Modulator with default check, FEC's, and modulation
         * scheme.
         */
        Modulator(OFDM& phy);

        /** @brief Construct a flexframegen with the given check, inner and
         * outer FEC's, and modulation schemed.
         * @param check The data validity check.
         * @param fec0 The inner forward error-correction scheme.
         * @param fec1 The outer forward error-correction scheme.
         * @param mod_scheme The modulation scheme scheme.
         */
        explicit Modulator(OFDM& phy,
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
        /** @brief Associated OFDM PHY. */
        OFDM& _phy;

        /** @brief Soft TX gain */
        float _g;

        /** @brief The liquid-dsp flexframegen object */
        ofdmflexframegen _fg;

        /** @brief The liquid-dsp ofdmflexframegenprops object associated with
         * this ofdmflexframegen.
         */
        ofdmflexframegenprops_s _fgprops;

        /** Update frame properties to match _fgprops. */
        void update_props(void);
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public PHY::Demodulator
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

        /** @brief Callback for received packets. */
        std::function<void(std::unique_ptr<RadioPacket>)> _callback;

        /** @brief The timestamp of the slot we are demodulating. */
        Clock::time_point _demod_start;

        /** @brief The offset (in samples) from the beggining of the slot at
         * which we started demodulating.
         */
        size_t _demod_off;

        /** @brief The liquid-dsp flexframesync object */
        ofdmflexframesync _fs;

        static int liquid_callback(unsigned char *  _header,
                                   int              _header_valid,
                                   unsigned char *  _payload,
                                   unsigned int     _payload_len,
                                   int              _payload_valid,
                                   framesyncstats_s _stats,
                                   void *           _userdata);

        void callback(unsigned char *  _header,
                      int              _header_valid,
                      unsigned char *  _payload,
                      unsigned int     _payload_len,
                      int              _payload_valid,
                      framesyncstats_s _stats);
    };

    /** @brief Construct an OFDM PHY.
     * @param M The number of subcarriers.
     * @param cp_len The cyclic prefix length
     * @param taper_len The taper length (OFDM symbol overlap)
     * @param p The subcarrier allocation (null, pilot, data). Should have
     * M entries.
     * @param check The data validity check.
     * @param fec0 The inner forward error-correction scheme.
     * @param fec1 The outer forward error-correction scheme.
     * @param mod_scheme The modulation scheme scheme.
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
