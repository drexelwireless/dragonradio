#ifndef PHY_H_
#define PHY_H_

#include <functional>
#include <queue>

#include "IQBuffer.hh"
#include "Packet.hh"
#include "SafeQueue.hh"
#include "phy/ModPacket.hh"

/** @brief A physical layer protocol that can provide a modulator and
 * demodulator.
 */
class PHY {
public:
    /** @brief Modulate IQ data. */
    class Modulator
    {
    public:
        Modulator(PHY &phy) : phy_(phy) {};
        virtual ~Modulator() {};

        /** @brief Modulate a packet to produce IQ samples.
         *  @param mpkt The ModPacket in which to place modulated samples.
         *  @param pkt The NetPacket to modulate.
         */
        virtual void modulate(ModPacket& mpkt, std::shared_ptr<NetPacket> pkt) = 0;

    protected:
        PHY &phy_;
    };

    /** @brief Demodulate IQ data.
     */
    class Demodulator
    {
    public:
        Demodulator(PHY &phy) : phy_(phy) {};
        virtual ~Demodulator() {};

        /** @brief Reset the internal state of the demodulator.
         * @brief timestamp The timestamp of IQ buffer from which the first
         * provided sample willcome.
         * @brief off The offset of the first provided sample.
         */
        virtual void reset(Clock::time_point timestamp, size_t off) = 0;

        /** @brief Demodulate IQ samples.
         * @param data IQ samples to demodulate.
         * @param count The number of samples to demodulate
         * @param callback The function to call with any demodulated packets. If
         * a bad packet is received, the argument will be nullptr.
         */
        virtual void demodulate(std::complex<float>* data,
                                size_t count,
                                std::function<void(std::unique_ptr<RadioPacket>)> callback) = 0;

    protected:
        PHY &phy_;
    };

    PHY() {}
    virtual ~PHY() {}

    /** @brief Return the minimum oversample rate (with respect to PHY
     * bandwidth) needed for demodulation
     * @return The RX oversample rate
     */
    virtual double getMinRXRateOversample(void) const = 0;

    /** @brief Return the minimum oversample rate (with respect to PHY
     * bandwidth) needed for modulation
     * @return The TX oversample rate
     */
    virtual double getMinTXRateOversample(void) const = 0;

    /** @brief Tell the PHY what RX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setRXRate(double rate)
    {
        rx_rate_ = rate;
    }

    /** @brief Get the PHY's RX sample rate.
     */
    virtual double getRXRate(void)
    {
        return rx_rate_;
    }

    /** @brief Tell the PHY what TX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setTXRate(double rate)
    {
        tx_rate_ = rate;
    }

    /** @brief Get the PHY's TX sample rate.
     */
    virtual double getTXRate(void)
    {
        return tx_rate_;
    }

    /** @brief Get PHY RX oversample rate.
     */
    virtual double getRXRateOversample(void)
    {
        return rx_rate_oversample_;
    }

    /** @brief Set PHY RX oversample rate.
     * @param rate The rate.
     */
    virtual void setRXRateOversample(double rate)
    {
        rx_rate_oversample_ = rate;
    }

    /** @brief Get PHY TX oversample rate.
     */
    virtual double getTXRateOversample(void)
    {
        return tx_rate_oversample_;
    }

    /** @brief Set PHY TX oversample rate.
     * @param rate The rate.
     */
    virtual void setTXRateOversample(double rate)
    {
        tx_rate_oversample_ = rate;
    }

    /** @brief Create a Modulator for this %PHY */
    virtual std::unique_ptr<Modulator> make_modulator(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::unique_ptr<Demodulator> make_demodulator(void) = 0;

protected:
    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief RX oversample rate */
    double rx_rate_oversample_;

    /** @brief TX oversample rate */
    double tx_rate_oversample_;
};

#endif /* PHY_H_ */
