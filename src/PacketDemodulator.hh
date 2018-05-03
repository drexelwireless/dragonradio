#ifndef PACKETDEMODULATOR_H_
#define PACKETDEMODULATOR_H_

#include "IQBuffer.hh"

/** @brief A packet demodulator. */
class PacketDemodulator
{
public:
    PacketDemodulator() {};
    virtual ~PacketDemodulator() {};

    /** @brief Set demodulation parameters.
     * @brief prev_samps The number of samples from the end of the previous slot
     * to demodulate.
     * @brief cur_samps The number of samples from the current slot to
     * demodulate.
     */
    virtual void setDemodParameters(const size_t prev_samps,
                                    const size_t cur_samps) = 0;

    /** @brief Add an IQ buffer to demodulate. */
    virtual void push(std::shared_ptr<IQBuf> buf) = 0;
};

#endif /* PACKETDEMODULATOR_H_ */
