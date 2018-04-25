#ifndef MODPACKET_HH_
#define MODPACKET_HH_

#include <sys/types.h>

#include "IQBuffer.hh"

/** A modulated data packet to be sent over the radio */
struct ModPacket
{
    ModPacket(void) : nsamples(0) {};

    /** @brief Append an IQ sample buffer. */
    void appendSamples(std::shared_ptr<IQBuf> buf)
    {
        nsamples += buf->size();
        samples.push_back(buf);
    }

    /** @brief Total number of modulated samples. */
    size_t nsamples;

    /** @brief  Buffers containing the modulated samples. Modulating a packet
     * can produce more than one IQ buffer!
     */
    std::vector<std::shared_ptr<IQBuf>> samples;
};

#endif /* MODPACKET_HH_ */
