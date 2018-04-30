#ifndef PHY_H_
#define PHY_H_

#include <queue>

#include "IQBuffer.hh"
#include "ModPacket.hh"
#include "Packet.hh"
#include "SafeQueue.hh"

/** @brief %PHY packet header. */
struct Header {
    NodeId   src;     /**< @brief Packet source node. */
    NodeId   dest;    /**< @brief Packet destination node. */
    PacketId pkt_id;  /**< @brief Packet identifier. */
    uint16_t pkt_len; /**< @brief Length of the packet payload. */
                      /**< The packet payload may be padded. This field gives
                       * the size of the non-padded portion of the payload.
                       */
};

/** @brief A physical layer protocol that can provide a modulator and
 * demodulator.
 */
class PHY {
public:
    /** @brief Modulate IQ data. */
    class Modulator
    {
    public:
        Modulator() {};
        virtual ~Modulator() {};

        /** @brief Set soft TX gain.
         * @param dB The soft gain in dB.
         */
        virtual void setSoftTXGain(float dB) = 0;

        /** @brief Modulate a packet to produce IQ samples.
         *  @param pkt The NetPacket to modulate.
         *  @return A ModPacket containing IQ samples.
         */
        virtual std::unique_ptr<ModPacket> modulate(std::unique_ptr<NetPacket> pkt) = 0;
    };

    /** @brief Demodulate IQ data.
     */
    class Demodulator
    {
    public:
        Demodulator() {};
        virtual ~Demodulator() {};

        /** @brief  Demodulate IQ samples, placing any demodulated packet into
         * the given queue.
         * @param buf the buffer of IQ samples.
         * @param q the queue in which to place demodulated packets.
         */
        virtual void demodulate(std::unique_ptr<IQQueue> buf, SafeQueue<std::unique_ptr<RadioPacket>>& q) = 0;
    };

    PHY() {}
    virtual ~PHY() {}

    /** @brief Return the IQ oversample rate (with respect to PHY bandwidth)
     * needed for demodulation
     * @return The RX oversample rate
     */
    virtual double getRxRateOversample(void) const = 0;

    /** @brief Return the IQ oversample rate (with respect to PHY bandwidth)
      * needed for modulation
      * @return The TX oversample rate
      */
    virtual double getTxRateOversample(void) const = 0;

    /** @brief Create a Modulator for this %PHY */
    virtual std::unique_ptr<Modulator> make_modulator(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::unique_ptr<Demodulator> make_demodulator(void) = 0;
};

#endif /* PHY_H_ */
