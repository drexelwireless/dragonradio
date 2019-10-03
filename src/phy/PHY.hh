#ifndef PHY_H_
#define PHY_H_

#include <atomic>
#include <functional>
#include <list>

#include "IQBuffer.hh"
#include "Logger.hh"
#include "Packet.hh"
#include "RadioConfig.hh"
#include "SafeQueue.hh"
#include "mac/Snapshot.hh"
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
        Modulator(PHY &phy) : phy_(phy) {}
        virtual ~Modulator() = default;

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param g Soft (multiplicative) gain to apply to modulated signal.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        virtual void modulate(std::shared_ptr<NetPacket> pkt,
                              const float gain,
                              ModPacket &mpkt) = 0;

    protected:
        /** @brief Our PHY */
        PHY &phy_;
    };

    /** @brief Demodulate IQ data.
     */
    class Demodulator
    {
    public:
        Demodulator(PHY &phy) : phy_(phy) {}
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
                                std::optional<ssize_t> snapshot_off,
                                ssize_t offset,
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

    protected:
        /** @brief Our PHY */
        PHY &phy_;
    };

    PHY(std::shared_ptr<SnapshotCollector> collector,
        NodeId node_id)
      : snapshot_collector_(collector)
      , node_id_(node_id)
      , rx_rate_(0.0)
      , tx_rate_(0.0)
    {
    }

    virtual ~PHY() = default;

    PHY() = delete;

    /** @brief Get the snapshot collector */
    const std::shared_ptr<SnapshotCollector> &getSnapshotCollector(void) const
    {
        return snapshot_collector_;
    }

    /** @brief Get this node's ID. */
    NodeId getNodeId(void) const
    {
        return node_id_;
    }

    /** @brief Get the PHY's RX sample rate. */
    double getRXRate(void)
    {
        return rx_rate_;
    }

    /** @brief Tell the PHY what RX sample rate we are running at.
     * @param rate The rate.
     */
    void setRXRate(double rate)
    {
        rx_rate_ = rate;
    }

    /** @brief Get the PHY's TX sample rate. */
    double getTXRate(void)
    {
        return tx_rate_;
    }

    /** @brief Tell the PHY what TX sample rate we are running at.
     * @param rate The rate.
     */
    void setTXRate(double rate)
    {
        tx_rate_ = rate;
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
    virtual std::shared_ptr<Modulator> mkModulator(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::shared_ptr<Demodulator> mkDemodulator(void) = 0;

    /** @brief Return flag indicating whether or not we want the given packet */
    /** We only demodulate packets destined for us *unless* we are collecting
     * snapshots, in which case we demodulate everything so we can correctly
     * record all known transmissions.
     */
    bool wantPacket(bool header_valid, const Header *h)
    {
        return header_valid
            && (h->curhop != node_id_)
            && ((h->nexthop == kNodeBroadcast) ||
                (h->nexthop == node_id_) ||
                (snapshot_collector_ && snapshot_collector_->active()));
    }

    /** @brief Create a radio packet from a header and payload */
    std::unique_ptr<RadioPacket> mkRadioPacket(bool header_valid,
                                               bool payload_valid,
                                               const Header *h,
                                               size_t payload_len,
                                               unsigned char *payload_data)
    {
        if (!header_valid) {
            if (rc.log_invalid_headers) {
                if (rc.verbose && !rc.debug)
                    fprintf(stderr, "HEADER INVALID\n");
                logEvent("PHY: invalid header");
            }

            return nullptr;
        } else if (!payload_valid) {
            std::unique_ptr<RadioPacket> pkt = std::make_unique<RadioPacket>(*h);

            pkt->internal_flags.invalid_payload = 1;

            if (h->nexthop == node_id_) {
                if (rc.verbose && !rc.debug)
                    fprintf(stderr, "PAYLOAD INVALID\n");
                logEvent("PHY: invalid payload: curhop=%u; nexthop=%u; seq=%u",
                    pkt->hdr.curhop,
                    pkt->hdr.nexthop,
                    (unsigned) pkt->hdr.seq);
            }

            return pkt;
        } else {
            std::unique_ptr<RadioPacket> pkt = std::make_unique<RadioPacket>(*h, payload_data, payload_len);

            if (!pkt->integrityIntact()) {
                pkt->internal_flags.invalid_payload = 1;

                if (rc.verbose && !rc.debug)
                    fprintf(stderr, "PAYLOAD INTEGRITY VIOLATED\n");
                logEvent("PHY: packet integrity not intact: seq=%u",
                    (unsigned) pkt->hdr.seq);
            }

            // Cache payload size if this packet is not compressed
            if (!pkt->hdr.flags.compressed)
                pkt->payload_size = pkt->getPayloadSize();

            return pkt;
        }
    }

protected:
    /** @brief Our snapshot collector */
    std::shared_ptr<SnapshotCollector> snapshot_collector_;

    /** @brief Node ID */
    const NodeId node_id_;

    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief TX sample rate */
    double tx_rate_;
};

#endif /* PHY_H_ */
