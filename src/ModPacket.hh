#ifndef MODPACKET_HH_
#define MODPACKET_HH_

#include <sys/types.h>

#include "IQBuffer.hh"

/** A modulated data packet to be sent over the radio */
struct ModPacket
{
    ModPacket(void) : nsamples(0) {};

    /** Append an IQ sample buffer */
    void appendSamples(std::unique_ptr<IQBuf> buf)
    {
        nsamples += buf->size();
        samples.push_back(std::move(buf));
    }

    /** Total number of modulated samples. */
    size_t nsamples;

    /** Buffers containing the modulated samples. Modulating a packet can
        produce more than one IQ buffer! */
    std::vector<std::unique_ptr<IQBuf>> samples;
};

#endif /* MODPACKET_HH_ */
