#ifndef LIQUID_H_
#define LIQUID_H_

#include <mutex>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Packet.hh"
#include "phy/Liquid.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief Creation of liquid objects is not re-rentrant, so we need to protect access
 * with a mutex.
 */
extern std::mutex liquid_mutex;

class LiquidDemodulator : public PHY::Demodulator {
public:
    LiquidDemodulator(std::shared_ptr<Net> net);
    virtual ~LiquidDemodulator();

protected:
    /** @brief The Net destination for demodulated packets. */
    std::shared_ptr<Net> net_;

    /** @brief Callback for received packets. */
    std::function<void(std::unique_ptr<RadioPacket>)> callback_;

    /** @brief Resampling factor. This is used to adjust demod_off_. */
    unsigned int resamp_fact_;

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
