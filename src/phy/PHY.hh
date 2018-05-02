#ifndef PHY_H_
#define PHY_H_

#include <functional>
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

        /** @brief Reset the internal state of the demodulator.
         * @brief timestamp The timestamp of IQ buffer from which the first
         * provided sample willcome.
         * @brief off The offset of the first provided sample.
         */
        virtual void reset(uhd::time_spec_t timestamp, size_t off) = 0;

        /** @brief Demodulate IQ samples.
         * @param data IQ samples to demodulate.
         * @param count The number of samples to demodulate
         * @param callback The function to call with any demodulated packets. If
         * a bad packet is received, the argument will be nullptr.
         */
        virtual void demodulate(std::complex<float>* data,
                                size_t count,
                                std::function<void(std::unique_ptr<RadioPacket>)> callback) = 0;
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

    /** @brief Tell the PHY what RX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setRxRate(double rate)
    {
        _rx_rate = rate;
    }

    /** @brief Tell the PHY what TX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setTxRate(double rate)
    {
        _tx_rate = rate;
    }

    /** @brief Create a Modulator for this %PHY */
    virtual std::unique_ptr<Modulator> make_modulator(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::unique_ptr<Demodulator> make_demodulator(void) = 0;

protected:
    /** @brief RX sample rate */
    double _rx_rate;

    /** @brief TX sample rate */
    double _tx_rate;
};

#endif /* PHY_H_ */