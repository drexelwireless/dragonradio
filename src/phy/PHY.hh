// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef PHY_H_
#define PHY_H_

#include <atomic>
#include <functional>
#include <list>

#include "logging.hh"
#include "IQBuffer.hh"
#include "Packet.hh"
#include "RadioConfig.hh"
#include "mac/Snapshot.hh"
#include "net/Net.hh"
#include "phy/AutoGain.hh"

/** @brief A modulated data packet to be sent over the radio */
struct ModPacket
{
    /** @brief Index of channel */
    unsigned chanidx;

    /** @brief Channel */
    Channel channel;

    /** @brief Offset of start of packet from start of slot, in number of
     * samples.
     */
    size_t start;

    /** @brief Offset of start of packet from beginning of sample buffer */
    size_t offset;

    /** @brief Number of modulated samples */
    size_t nsamples;

    /** @brief Modulation latency (sec) */
    double mod_latency;

    /** @brief Buffer containing the modulated samples. */
    std::shared_ptr<IQBuf> samples;

    /** @brief The un-modulated packet. */
    std::shared_ptr<NetPacket> pkt;
};

/** @brief A physical layer protocol that can provide a modulator and
 * demodulator.
 */
class PHY {
public:
    /** @brief Modulate packets. */
    class PacketModulator
    {
    public:
        PacketModulator(PHY &phy) : phy_(phy) {}
        virtual ~PacketModulator() = default;

        /** @brief Modulate a packet to produce IQ samples.
         * @param pkt The NetPacket to modulate.
         * @param gain Soft (multiplicative) gain to apply to modulated signal.
         * @param mpkt The ModPacket in which to place modulated samples.
         */
        virtual void modulate(std::shared_ptr<NetPacket> pkt,
                              const float gain,
                              ModPacket &mpkt) = 0;

    protected:
        /** @brief Our PHY */
        PHY &phy_;
    };

    /** @brief Demodulate packets.
     */
    class PacketDemodulator
    {
    public:
        using callback_type = std::function<void(std::shared_ptr<RadioPacket>&&)>;

        PacketDemodulator(PHY &phy) : phy_(phy) {}
        virtual ~PacketDemodulator() = default;

        /** @brief Set demodulation callback */
        void setCallback(callback_type callback)
        {
            callback_ = callback;
        }

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
         * @param offset The offset of the first sample that will be
         * demodulated. Can be negative!
         * @param delay Filter delay
         * @param rate The rate of the resampler applied before data is passed
         * to the demodulator.
         * @param rx_rate The RX rate (Hz).
         */
        virtual void timestamp(const MonoClock::time_point &timestamp,
                               std::optional<ssize_t> snapshot_off,
                               ssize_t offset,
                               size_t delay,
                               float rate,
                               float rx_rate) = 0;

        /** @brief Demodulate IQ samples.
         * @param data The IQ data to demodulate
         * @param count The number of samples to demodulate
         */
        virtual void demodulate(const std::complex<float>* data,
                                size_t count) = 0;

    protected:
        /** @brief Our PHY */
        PHY &phy_;

        /** @brief Demodulation callback */
        callback_type callback_;
    };

    PHY() = default;

    virtual ~PHY() = default;

    /** @brief MCS entry */
    struct MCSEntry {
        /** @brief MCS */
        const MCS *mcs;

        /** @brief auto-gain for this MCS */
        AutoGain autogain;

        /** @brief Is this entry valid? */
        bool valid;
    };

    /** @brief MCS table */
    std::vector<MCSEntry> mcs_table;

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
    virtual size_t getModulatedSize(mcsidx_t mcsidx, size_t n) = 0;

    /** @brief Create a Modulator for this %PHY */
    virtual std::shared_ptr<PacketModulator> mkPacketModulator(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::shared_ptr<PacketDemodulator> mkPacketDemodulator(void) = 0;

    /** @brief Return flag indicating whether or not we want the given packet */
    /** We only demodulate packets destined for us *unless* we are collecting
     * snapshots, in which case we demodulate everything so we can correctly
     * record all known transmissions.
     */
    static inline bool wantPacket(bool header_valid, const Header *h)
    {
        return header_valid
            && (h->curhop != node_id_)
            && ((h->nexthop == kNodeBroadcast) ||
                (h->nexthop == node_id_) ||
                (rc.snapshot_collector && rc.snapshot_collector->active()));
    }

    /** @brief Create a radio packet from a header and payload */
    static std::shared_ptr<RadioPacket> mkRadioPacket(bool header_valid,
                                                      bool payload_valid,
                                                      const Header &h,
                                                      size_t payload_len,
                                                      unsigned char *payload_data)
    {
        if (!header_valid) {
            if (log_invalid_headers_)
                logPHY(LOGINFO, "invalid header");

            return nullptr;
        } else if (!payload_valid) {
            std::shared_ptr<RadioPacket> pkt = std::make_shared<RadioPacket>(h);

            pkt->internal_flags.invalid_payload = 1;

            if (h.nexthop == node_id_)
                logPHY(LOGINFO, "invalid payload: curhop=%u; nexthop=%u; seq=%u",
                    pkt->hdr.curhop,
                    pkt->hdr.nexthop,
                    (unsigned) pkt->hdr.seq);

            return pkt;
        } else {
            std::shared_ptr<RadioPacket> pkt = std::make_shared<RadioPacket>(h, payload_data, payload_len);

            if (!pkt->integrityIntact()) {
                pkt->internal_flags.invalid_payload = 1;

                logPHY(LOGINFO, "packet integrity not intact: seq=%u",
                    (unsigned) pkt->hdr.seq);
            }

            // Cache payload size if this packet is not compressed
            if (!pkt->hdr.flags.compressed)
                pkt->payload_size = pkt->getPayloadSize();

            return pkt;
        }
    }

    /** @brief Get this node's ID */
    static NodeId getNodeId()
    {
        return node_id_;
    }

    /** @brief Set this node's ID */
    static void setNodeId(NodeId id)
    {
        node_id_ = id;
    }

    /** @brief Get whether or not invalid headers should be logged */
    static bool getLogInvalidHeaders()
    {
        return log_invalid_headers_;
    }

    /** @brief Set whether or not invalid headers should be logged */
    static void setLogInvalidHeaders(bool log)
    {
        log_invalid_headers_ = log;
    }

protected:
    /** @brief This node's ID */
    static NodeId node_id_;

    /** @brief Log invalid headers? */
    static bool log_invalid_headers_;
};

#endif /* PHY_H_ */
