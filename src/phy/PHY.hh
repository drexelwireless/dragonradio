#ifndef PHY_H_
#define PHY_H_

#include <atomic>
#include <functional>
#include <list>

#include "IQBuffer.hh"
#include "Packet.hh"
#include "SafeQueue.hh"
#include "phy/ModPacket.hh"

/** @brief A physical layer protocol that can provide a modulator and
 * demodulator.
 */
class PHY {
public:
    /** @brief A modulator or demodulator, a.k.a a *ulator. */
    class Ulator
    {
    public:
        Ulator(PHY &phy)
          : phy_(phy)
          , pending_reconfigure_(false)
          {
          }

        virtual ~Ulator() = default;

        virtual void scheduleReconfigure(void)
        {
            pending_reconfigure_.store(true, std::memory_order_relaxed);
        }

    protected:
        /** @brief Our PHY */
        /** We keep a reference to our PHY so that we can query it for rate
         * information.
         */
        PHY &phy_;

        /** @brief A flag indicating that our configuration has changed, so we
         * should update it
         */
        std::atomic<bool> pending_reconfigure_;

        /** @brief Reconfigure the modulator/demodulator based on new PHY
         * parameters.
         */
        virtual void reconfigure(void) = 0;
    };

    /** @brief Modulate IQ data. */
    class Modulator : public Ulator
    {
    public:
        Modulator(PHY &phy) : Ulator(phy) {};
        virtual ~Modulator() = default;

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        virtual void modulate(std::shared_ptr<NetPacket> pkt,
                              ModPacket &mpkt) = 0;
    };

    /** @brief Demodulate IQ data.
     */
    class Demodulator : public Ulator
    {
    public:
        Demodulator(PHY &phy) : Ulator(phy) {};
        virtual ~Demodulator() = default;

        /** @brief Is a frame currently being demodulated?
         * @return true if a frame is currently being demodulated, false
         * otherwise.
         */
        virtual bool isFrameOpen(void) = 0;

        /** @brief Reset the internal state of the demodulator.
         * @param channel The channel being demodulated.
         */
        virtual void reset(const Channel &channel) = 0;

        /** @brief Set timestamp for demodulation
         * @param timestamp The timestamp for future samples.
         * @param snapshot_off The snapshot offset associated with the given
         * timestamp.
         * @param offset The offset of the first sample that will be demodulated.
         * @param rate The rate of the resampler applied before data is passed
         * to the demodulator.
         */
         virtual void timestamp(const MonoClock::time_point &timestamp,
                                std::optional<size_t> snapshot_off,
                                size_t offset,
                                float rate) = 0;

        /** @brief Demodulate IQ samples.
         * @param data The IQ data to demodulate
         * @param count The number of samples to demodulate
         * @param callback The function to call with any demodulated packets. If
         * a bad packet is received, the argument will be nullptr.
         */
         virtual void demodulate(const std::complex<float>* data,
                                 size_t count,
                                 std::function<void(std::unique_ptr<RadioPacket>)> callback) = 0;
    };

    PHY(NodeId node_id)
      : node_id_(node_id)
      , rx_rate_(0.0)
      , tx_rate_(0.0)
    {
    }

    virtual ~PHY() = default;

    PHY() = delete;

    /** @brief Get this node's ID. */
    NodeId getNodeId(void)
    {
        return node_id_;
    }

    /** @brief Get the PHY's RX sample rate. */
    virtual double getRXRate(void)
    {
        return rx_rate_;
    }

    /** @brief Tell the PHY what RX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setRXRate(double rate)
    {
        rx_rate_ = rate;
        reconfigureRX();
    }

    /** @brief Get the PHY's TX sample rate. */
    virtual double getTXRate(void)
    {
        return tx_rate_;
    }

    /** @brief Tell the PHY what TX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setTXRate(double rate)
    {
        tx_rate_ = rate;
        reconfigureTX();
    }

    /** @brief Return the minimum oversample rate (with respect to PHY
     * bandwidth) needed for demodulation
     * @return The minimum RX oversample rate
     */
    virtual unsigned getMinRXRateOversample(void) const = 0;

    /** @brief Return the minimum oversample rate (with respect to PHY
     * bandwidth) needed for modulation
     * @return The minimum TX oversample rate
     */
    virtual unsigned getMinTXRateOversample(void) const = 0;

    /** @brief Calculate size of modulated data */
    virtual size_t getModulatedSize(const TXParams &params, size_t n) = 0;

    /** @brief Create a Modulator for this %PHY */
    virtual std::shared_ptr<Modulator> mkModulator(void)
    {
        auto p = mkModulatorInternal();

        modulators_.push_back(p);
        return p;
    }

    /** @brief Create a Demodulator for this %PHY */
    virtual std::shared_ptr<Demodulator> mkDemodulator(void)
    {
        auto p = mkDemodulatorInternal();

        demodulators_.push_back(p);
        return p;
    }

protected:
    /** @brief Node ID */
    const NodeId node_id_;

    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Modulators */
    std::list<std::weak_ptr<Modulator>> modulators_;

    /** @brief Demodulators */
    std::list<std::weak_ptr<Demodulator>> demodulators_;

    /** @brief Create a Modulator for this %PHY */
    virtual std::shared_ptr<Modulator> mkModulatorInternal(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::shared_ptr<Demodulator> mkDemodulatorInternal(void) = 0;

    /** @brief Reconfigure for new RX parameters */
    virtual void reconfigureRX(void)
    {
        auto it = demodulators_.begin();

        while (it != demodulators_.end()) {
            auto p = it->lock();

            if (p) {
                p->scheduleReconfigure();
                it++;
            } else
                it = demodulators_.erase(it);
        }
    }

    /** @brief Reconfigure for new TX parameters */
    virtual void reconfigureTX(void)
    {
        auto it = modulators_.begin();

        while (it != modulators_.end()) {
            auto p = it->lock();

            if (p) {
                p->scheduleReconfigure();
                it++;
            } else
                it = modulators_.erase(it);
        }
    }
};

#endif /* PHY_H_ */
