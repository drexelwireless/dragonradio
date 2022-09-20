// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef PHY_H_
#define PHY_H_

#include <atomic>
#include <functional>
#include <list>

#include "IQBuffer.hh"
#include "Packet.hh"
#include "mac/Snapshot.hh"
#include "phy/AutoGain.hh"

/* When non-zero, nodes will only listen to other nodes whose node ID differs
 * from theirs by 1. This makes it easy to set up a debug configuration where
 * nodes act like they are in a linear layout in which nodes can only hear their
 * immediate neighbors. Such a layout is nice for debugging MANET operation.
 */
#define DEBUG_LINEAR_LAYOUT 0

/** @brief Complex float */
using C = std::complex<float>;

/** @brief FIR taps */
using Taps = std::vector<C>;

/** @brief EVM threshold */
using evm_thresh_t = std::optional<float>;

class PHY;

/** @brief A PHY channel configuration */
struct PHYChannel {
    PHYChannel() = delete;

    PHYChannel(const Channel& channel_,
               std::shared_ptr<PHY> phy_,
               const std::vector<evm_thresh_t>& evm_thresh_)
      : channel(channel_)
      , phy(phy_)
      , evm_thresh(evm_thresh_)
      , I(1)
      , D(1)
    {
    }

    /** @brief The channel */
    Channel channel;

    /** @brief PHY for channel */
    std::shared_ptr<PHY> phy;

    /** @brief EVM threshold table */
    std::vector<evm_thresh_t> evm_thresh;

    /** @brief Interpolation rate */
    unsigned I;

    /** @brief Decimation rate */
    unsigned D;

    /** @brief FIR filter taps */
    Taps taps;
};

/** @brief A modulated data packet to be sent over the radio */
struct ModPacket
{
    /** @brief Index of channel */
    unsigned chanidx;

    /** @brief Channel */
    Channel channel;

    /** @brief Offset of start of packet from beginning of TX record */
    size_t start;

    /** @brief Offset of start of packet from beginning of sample buffer */
    size_t offset;

    /** @brief Number of modulated samples */
    size_t nsamples;

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

        PacketDemodulator(PHY &phy, unsigned chanidx, const Channel &channel)
          : phy_(phy)
          , chanidx_(chanidx)
          , channel_(channel)
        {
        }

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

        /** @brief Index of channel we are demodulating */
        unsigned chanidx_;

        /** @brief Channel we are demodulating */
        Channel channel_;

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
    };

    /** @brief MCS table */
    std::vector<MCSEntry> mcs_table;

    /** @brief Return the minimum oversample rate (with respect to PHY
     * bandwidth) needed for demodulation
     * @return The minimum RX oversample rate
     */
    virtual unsigned getRXOversampleFactor(void) const = 0;

    /** @brief Return the minimum oversample rate (with respect to PHY
     * bandwidth) needed for modulation
     * @return The minimum TX oversample rate
     */
    virtual unsigned getTXOversampleFactor(void) const = 0;

    /** @brief Calculate size of modulated data */
    virtual size_t getModulatedSize(mcsidx_t mcsidx, size_t n) = 0;

    /** @brief Create a Modulator for this %PHY */
    virtual std::shared_ptr<PacketModulator> mkPacketModulator(void) = 0;

    /** @brief Create a Demodulator for this %PHY */
    virtual std::shared_ptr<PacketDemodulator> mkPacketDemodulator(unsigned chanidx, const Channel &channel) = 0;

    /** @brief Return flag indicating whether or not we want the given packet */
    /** We only demodulate packets destined for us *unless* we are collecting
     * snapshots, in which case we demodulate everything so we can correctly
     * record all known transmissions.
     */
    static inline bool wantPacket(bool header_valid, const Header *h)
    {
        return header_valid
            && (h->flags.team == team_)
            && (h->curhop != node_id_)
#if DEBUG_LINEAR_LAYOUT
            && (h->curhop == node_id_ - 1 || h->curhop == node_id_ + 1)
#endif /* DEBUG_LINEAR_LAYOUT */
            && ((h->nexthop == kNodeBroadcast) ||
                (h->nexthop == node_id_) ||
                (snapshot_collector_ && snapshot_collector_->active()));
    }

    /** @brief Create a radio packet from a header and payload */
    static std::shared_ptr<RadioPacket> mkRadioPacket(bool header_valid,
                                                      bool payload_valid,
                                                      const Header &h,
                                                      size_t payload_len,
                                                      unsigned char *payload_data);

    /** @brief Get this node's ID */
    static NodeId getTeam()
    {
        return team_;
    }

    /** @brief Set this node's ID */
    static void setTeam(uint8_t team)
    {
        team_ = team;
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

    /** @brief Set snapshot collector */
    static void setSnapshotCollector(std::shared_ptr<SnapshotCollector> collector)
    {
        snapshot_collector_ = collector;
    }

    /** @brief Reset snapshot collector */
    static void resetSnapshotCollector(void)
    {
        snapshot_collector_.reset();
    }

protected:
    /** @brief This node's team */
    static uint8_t team_;

    /** @brief This node's ID */
    static NodeId node_id_;

    /** @brief Log invalid headers? */
    static bool log_invalid_headers_;

    /** @brief Snapshot collector */
    static std::shared_ptr<SnapshotCollector> snapshot_collector_;
};

#endif /* PHY_H_ */
