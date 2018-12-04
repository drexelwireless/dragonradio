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
         * @param shift Samples should be shifted by this frequency (Hz) during
         * modulation
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        virtual void modulate(std::shared_ptr<NetPacket> pkt,
                              double shift,
                              ModPacket &mpkt) = 0;
    };

    /** @brief Demodulate IQ data.
     */
    class Demodulator : public Ulator
    {
    public:
        Demodulator(PHY &phy) : Ulator(phy) {};
        virtual ~Demodulator() = default;

        /** @brief Reset the internal state of the demodulator.
         * @brief timestamp The timestamp of IQ buffer from which the first
         * provided sample willcome.
         * @brief off The offset of the first provided sample.
         */
        virtual void reset(Clock::time_point timestamp,
                           size_t off) = 0;

        /** @brief Set the snapshot offset.
        * @brief snapshot_off The current snapshot offset.
        */
        virtual void setSnapshotOffset(ssize_t snapshot_off) = 0;

        /** @brief Demodulate IQ samples.
         * @param data IQ samples to demodulate.
         * @param count The number of samples to demodulate
         * @param shift Samples should be shifted by this frequency (Hz) before
         * demodulation
         * @param callback The function to call with any demodulated packets. If
         * a bad packet is received, the argument will be nullptr.
         */
        virtual void demodulate(std::complex<float>* data,
                                size_t count,
                                double shift,
                                std::function<void(std::unique_ptr<RadioPacket>)> callback) = 0;
    };

    PHY(NodeId node_id)
      : node_id_(node_id)
      , rx_rate_(0.0)
      , tx_rate_(0.0)
      , rx_rate_oversample_(0.0)
      , tx_rate_oversample_(0.0)
    {
    }

    virtual ~PHY() = default;

    PHY() = delete;

    /** @brief Get this node's ID. */
    NodeId getNodeId(void)
    {
        return node_id_;
    }

    /** @brief Tell the PHY what RX sample rate we are running at.
     * @param rate The rate.
     */
    virtual void setRXRate(double rate)
    {
        rx_rate_ = rate;
        reconfigureRX();
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
        reconfigureTX();
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
        reconfigureRX();
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
        reconfigureTX();
    }

    /** @brief Get TX upsample rate. */
    virtual double getTXUpsampleRate(void)
    {
        return getTXRateOversample()/getMinTXRateOversample();
    }

    /** @brief Get RX downsample rate. */
    virtual double getRXDownsampleRate(void)
    {
        return getMinRXRateOversample()/getRXRateOversample();
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
    double node_id_;

    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief RX oversample rate */
    double rx_rate_oversample_;

    /** @brief TX oversample rate */
    double tx_rate_oversample_;

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
