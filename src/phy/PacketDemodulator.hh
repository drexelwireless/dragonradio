#ifndef PACKETDEMODULATOR_H_
#define PACKETDEMODULATOR_H_

#include "IQBuffer.hh"
#include "phy/Channels.hh"

/** @brief A packet demodulator. */
class PacketDemodulator
{
public:
    PacketDemodulator(std::shared_ptr<Channels> channels)
        : channels_(channels)
    {
    }

    virtual ~PacketDemodulator() {};

    /** @brief Set demodulation parameters.
     * @brief prev_samps The number of samples from the end of the previous slot
     * to demodulate.
     * @brief cur_samps The number of samples from the current slot to
     * demodulate.
     */
    virtual void setWindowParameters(const size_t prev_samps,
                                     const size_t cur_samps) = 0;

    /** @brief Add an IQ buffer to demodulate.
     * @param buf The IQ samples to demodulate
     */
    virtual void push(std::shared_ptr<IQBuf> buf) = 0;

protected:
    /** @brief Radio channels, given as shift from center frequency */
    std::shared_ptr<Channels> channels_;
};

#endif /* PACKETDEMODULATOR_H_ */
