#ifndef LIQUID_H_
#define LIQUID_H_

#include <mutex>

#include <liquid/liquid.h>

#include "Clock.hh"
#include "Packet.hh"
#include "phy/PHY.hh"

/** @brief Creation of liquid objects is not re-rentrant, so we need to protect access
 * with a mutex.
 */
extern std::mutex liquid_mutex;

class LiquidDemodulator : public PHY::Demodulator {
public:
    LiquidDemodulator(std::function<bool(Header&)> predicate);
    virtual ~LiquidDemodulator();

protected:
    /** @brief Predicate to filter received received packets. */
    std::function<bool(Header&)> _predicate;

    /** @brief Callback for received packets. */
    std::function<void(std::unique_ptr<RadioPacket>)> _callback;

    /** @brief Resampling factor. This is used to adjust _demod_off. */
    unsigned int _resamp_fact;

    /** @brief The timestamp of the slot we are demodulating. */
    Clock::time_point _demod_start;

    /** @brief The offset (in samples) from the beggining of the slot at
     * which we started demodulating.
     */
    size_t _demod_off;

    static int liquid_callback(unsigned char *  _header,
                               int              _header_valid,
                               unsigned char *  _payload,
                               unsigned int     _payload_len,
                               int              _payload_valid,
                               framesyncstats_s _stats,
                               void *           _userdata);

    virtual int callback(unsigned char *  _header,
                         int              _header_valid,
                         unsigned char *  _payload,
                         unsigned int     _payload_len,
                         int              _payload_valid,
                         framesyncstats_s _stats);
};

#endif /* LIQUID_H_ */
