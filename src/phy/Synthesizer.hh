#ifndef SYNTHESIZER_H_
#define SYNTHESIZER_H_

#include "Logger.hh"
#include "net/Net.hh"
#include "phy/ModPacket.hh"
#include "phy/PHY.hh"

/** @brief Base class for synthesizers */
class Synthesizer : public Element
{
public:
    Synthesizer(std::shared_ptr<PHY> phy,
                double tx_rate)
      : sink(*this, nullptr, nullptr)
      , phy_(phy)
      , tx_rate_(tx_rate)
      , max_packet_size_(0)
    {
    }

    virtual ~Synthesizer() = default;

    /** @brief Get the TX sample rate. */
    virtual double getTXRate(void)
    {
        return tx_rate_;
    }

    /** @brief Set the TX sample rate.
     * @param rate The rate.
     */
    virtual void setTXRate(double rate)
    {
        tx_rate_ = rate;
        reconfigure();
    }

    /** @brief Get maximum packet size. */
    size_t getMaxPacketSize(void)
    {
        return max_packet_size_;
    }

    /** @brief Set maximum packet size. */
    void setMaxPacketSize(size_t max_packet_size)
    {
        max_packet_size_ = max_packet_size;
    }

    /** @brief Get the maximum modulation upsample rate. */
    /** This should return the maximum upsample rate used during modulation.
     * This value is used by SmartController to estimate the maximum number of
     * packets that can fit in one time slot.
     */
    virtual double getMaxTXUpsampleRate(void) = 0;

    /** @brief Modulate one packet.
     * @param pkt The NetPacket to modulate.
     * @param mpkt The ModPacket that will hold the modulated packet.
     */
    virtual void modulateOne(std::shared_ptr<NetPacket> pkt,
                             ModPacket &mpkt) = 0;

    /** @brief Modulate samples.
     * @param n The number of samples to produce.
     */
    virtual void modulate(size_t n) = 0;

    /** @brief Pop a list of modulated packet such that the total number of
     * modulated samples is maxSamples or fewer.
     * @param pkts A reference to a list to which the popped packets will be
     * appended.
     * @param maxSample The maximum number of samples to pop.
     * @param overfill Completely fill the slot, even if it means overfilling it
     * @return The number of samples popped
     */
    virtual size_t pop(std::list<std::unique_ptr<ModPacket>>& pkts,
                       size_t maxSamples,
                       bool overfill) = 0;

    /** @brief Reconfigure for new TX parameters */
    virtual void reconfigure(void) = 0;

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

protected:
    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Maximum number of possible samples in a modulated packet. */
    size_t max_packet_size_;
};

#endif /* SYNTHESIZER_H_ */
