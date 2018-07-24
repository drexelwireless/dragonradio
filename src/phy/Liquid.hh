#ifndef LIQUID_H_
#define LIQUID_H_

#include <mutex>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Packet.hh"
#include "phy/PHY.hh"

/** @brief Creation of liquid objects is not re-rentrant, so we need to protect
 * access with a mutex.
 */
extern std::mutex liquid_mutex;

class LiquidPHY : public PHY {
public:
    LiquidPHY(const MCS &header_mcs, bool soft_header, bool soft_payload);
    LiquidPHY();
    virtual ~LiquidPHY();

    LiquidPHY(const LiquidPHY&) = delete;
    LiquidPHY(LiquidPHY&&) = delete;

    LiquidPHY& operator=(const LiquidPHY&) = delete;
    LiquidPHY& operator=(LiquidPHY&&) = delete;

    /** @brief Return modulation and coding scheme used for headers. */
    const MCS &getHeaderMCS() const
    {
        return header_mcs_;
    }

    /** @brief Return flag indicating whether or not to use soft-decoding for
      * headers.
      */
    bool getSoftHeader() const
    {
        return soft_header_;
    }

    /** @brief Return flag indicating whether or not to use soft-decoding for
      * payload.
      */
    bool getSoftPayload() const
    {
        return soft_payload_;
    }

protected:
    /** @brief Modulation and coding scheme for headers. */
    const MCS header_mcs_;

    /** @brief Flag indicating whether or not to use soft-decoding for headers.
      */
    const bool soft_header_;

    /** @brief Flag indicating whether or not to use soft-decoding for payload.
      */
    const bool soft_payload_;
};

class LiquidModulator : public PHY::Modulator {
public:
    LiquidModulator(LiquidPHY &phy);
    virtual ~LiquidModulator();

    LiquidModulator(const LiquidModulator&) = delete;
    LiquidModulator(LiquidModulator&&) = delete;

    LiquidModulator& operator=(const LiquidModulator&) = delete;
    LiquidModulator& operator=(LiquidModulator&&) = delete;
};

class LiquidDemodulator : public PHY::Demodulator {
public:
    LiquidDemodulator(LiquidPHY &phy);
    virtual ~LiquidDemodulator();

    LiquidDemodulator(const LiquidDemodulator&) = delete;
    LiquidDemodulator(LiquidDemodulator&&) = delete;

    LiquidDemodulator& operator=(const LiquidDemodulator&) = delete;
    LiquidDemodulator& operator=(LiquidDemodulator&&) = delete;

protected:
    /** @brief Callback for received packets. */
    std::function<void(std::unique_ptr<RadioPacket>)> callback_;

    /** @brief Internal resampling factor. */
    /** This is the factor by which the PHY internally oversamples, i.e., the
     * samples seen by the Liquid demodulator are decimated by this amount. We
     * need this quantity in order to properly track demod_off_ and friends.
     */
    unsigned int internal_oversample_fact_;

    /** @brief The timestamp of the slot we are demodulating. */
    Clock::time_point demod_start_;

    /** @brief The offset (in samples) from the beggining of the slot at
     * which we started demodulating.
     */
    size_t demod_off_;

    static int liquid_callback(unsigned char *  header_,
                               int              header_valid_,
                               unsigned char *  payload_,
                               unsigned int     payload_len_,
                               int              payload_valid_,
                               framesyncstats_s stats_,
                               void *           userdata_);

    virtual int callback(unsigned char *  header_,
                         int              header_valid_,
                         unsigned char *  payload_,
                         unsigned int     payload_len_,
                         int              payload_valid_,
                         framesyncstats_s stats_);
};

#endif /* LIQUID_H_ */
