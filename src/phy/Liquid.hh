#ifndef LIQUID_H_
#define LIQUID_H_

#include <mutex>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Packet.hh"
#include "phy/Liquid.hh"
#include "phy/PHY.hh"

/** @brief Creation of liquid objects is not re-rentrant, so we need to protect
 * access with a mutex.
 */
extern std::mutex liquid_mutex;

class LiquidDemodulator : public PHY::Demodulator {
public:
    LiquidDemodulator();
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
