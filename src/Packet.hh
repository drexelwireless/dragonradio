#ifndef PACKET_HH_
#define PACKET_HH_

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

#include <cstddef>
#include <iterator>
#include <vector>

#include <liquid/liquid.h>

#include "buffer.hh"
#include "Clock.hh"
#include "Seq.hh"
#include "phy/TXParams.hh"

typedef uint8_t NodeId;

enum {
    /** @brief Set if the packet is the first in a new connection */
    kSYN = 0,

    /** @brief Set if the packet is ACKing */
    kACK,

    /** @brief Set if the packet is NAKing */
    kNAK,

    /** @brief Set if this is a broadcast packet */
    kBroadcast,

    /** @brief Set if the packet has control data */
    kControl
};

typedef uint16_t PacketFlags;

enum {
    /** @brief Set if the packet has an assigned sequence number */
    kHasSeq = 0,

    /** @brief Set if the packet belongs to the internal IP network (10.*) */
    kIntNet,

    /** @brief Set if the packet belongs to the external IP network (192.168.*) */
    kExtNet,

    /** @brief Set if the packet had invalid header */
    kInvalidHeader,

    /** @brief Set if the packet had invalid payload */
    kInvalidPayload
};

typedef uint16_t InternalFlags;

/** @brief A time */
struct Time {
    uint64_t secs;
    double frac_secs;

    void from_wall_time(Clock::time_point t)
    {
        secs = t.t.get_full_secs();
        frac_secs = t.t.get_frac_secs();
    }

    Clock::time_point to_wall_time(void) const
    {
        return Clock::time_point { uhd::time_spec_t { static_cast<time_t>(secs), frac_secs } };
    }
};

/** @brief A Control message */
struct ControlMsg {
    enum Type {
        kHello,
        kTimestamp,
        kTimestampDelta
    };

    struct Hello {
        bool is_gateway;
    };

    struct Timestamp {
        Seq epoch;
        Time t;
    };

    struct TimestampDelta {
        /** @brief Node ID of sender */
        NodeId node;
        /** @brief Epoch of sender */
        Seq epoch;
        /** @brief Delta between receiver and sender */
        Time delta;
    };

    Type type;

    union {
        Hello hello;
        Timestamp timestamp;
        TimestampDelta timestamp_delta;
    };
};

/** @brief %PHY packet header. */
struct Header {
    /** @brief Current hop. */
    NodeId curhop;

    /** @brief Next hop. */
    NodeId nexthop;

    /** @brief Packet flags. */
    PacketFlags flags;

    /** @brief Packet sequence number. */
    Seq seq;

    /** @brief Length of the packet payload. */
    /** The packet payload may be padded or contain control data. This field
     * gives the size of the data portion of the payload.
     */
    uint16_t data_len;
};

/** @brief Extended header that appears in radio payload. */
struct ExtendedHeader {
    /** @brief Source */
    NodeId src;

    /** @brief Destination */
    NodeId dest;

    /** @brief Sequence number we are ACK'ing or NAK'ing. */
    Seq ack;
};

/** @brief A packet. */
struct Packet : public buffer<unsigned char>
{
    class iterator : public std::iterator<std::input_iterator_tag, ControlMsg> {
    public:
        iterator(const Packet &pkt);
        iterator(const Packet &pkt, int);
        ~iterator() = default;

        iterator() = delete;

        bool operator ==(const iterator& other);
        bool operator !=(const iterator& other);

        iterator& operator ++();
        iterator& operator ++(int);

        const ControlMsg &operator *();
        const ControlMsg *operator ->();

    private:
        const Packet &pkt_;
        uint16_t ctrl_len_;
        const unsigned char *ctrl_ptr_;
        ControlMsg ctrl_;
    };

    Packet() : buffer(), flags(0), seq(Seq::max()), internal_flags(0) {};
    Packet(size_t n) : buffer(n), flags(0), seq(Seq::max()), internal_flags(0) {};
    Packet(unsigned char* data, size_t n) : buffer(data, n), flags(0), seq(Seq::max()), internal_flags(0) {}

    /** @brief Current hop */
    /** If the packet originated in the network, this should be the current
     * node.
     */
    NodeId curhop;

    /** @brief Next hop */
    /** If the packet originated from the radio, this should be the current
     * node.
     */
    NodeId nexthop;

    /** @brief Packet flags. */
    PacketFlags flags;

    /** @brief Sequence number */
    Seq seq;

    /** @brief Length of data portion of the packet */
    uint16_t data_len;

    /** @brief Source */
    NodeId src;

    /** @brief Destination */
    NodeId dest;

    /** @brief Packet timestamp */
    Clock::time_point timestamp;

    /** @brief Set a flag */
    void setFlag(unsigned f)
    {
        flags |= (1 << f);
    }

    /** @brief Clear a flag */
    void clearFlag(unsigned f)
    {
        flags &= ~(1 << f);
    }

    /** @brief Test if a flag is set */
    bool isFlagSet(unsigned f) const
    {
        return flags & (1 << f);
    }

    /** @brief Internal flags. */
    InternalFlags internal_flags;

    /** @brief Set a flag */
    void setInternalFlag(unsigned f)
    {
        internal_flags |= (1 << f);
    }

    /** @brief Clear a flag */
    void clearInternalFlag(unsigned f)
    {
        internal_flags &= ~(1 << f);
    }

    /** @brief Test if a flag is set */
    bool isInternalFlagSet(unsigned f) const
    {
        return internal_flags & (1 << f);
    }

    /** @brief Get extended header */
    ExtendedHeader &getExtendedHeader(void)
    {
        return *reinterpret_cast<ExtendedHeader*>(data());
    }

    /** @brief Copy internal values to a PHY header */
    void toHeader(Header &hdr)
    {
        ExtendedHeader &ehdr = getExtendedHeader();

        hdr.curhop = curhop;
        hdr.nexthop = nexthop;
        hdr.flags = flags;
        hdr.seq = seq;
        hdr.data_len = data_len;

        ehdr.src = src;
        ehdr.dest = dest;
    }

    /** @brief Copy values from PHY header to packet */
    void fromHeader(Header &hdr)
    {
        curhop = hdr.curhop;
        nexthop = hdr.nexthop;
        flags = hdr.flags;
        seq = hdr.seq;
        data_len = std::min(hdr.data_len, static_cast<uint16_t>(sizeof(ExtendedHeader) + size()));
    }

    /** @brief Copy values from PHY header to packet */
    void fromExtendedHeader(void)
    {
        ExtendedHeader &ehdr = getExtendedHeader();

        src = ehdr.src;
        dest = ehdr.dest;
    }

    /** @brief Append a control message to a packet */
    void appendControl(const ControlMsg &ctrl);

    /** @brief Append a Hello control message to a packet */
    void appendHello(const ControlMsg::Hello &hello);

    /** @brief Append a Timestamp control message to a packet */
    void appendTimestamp(const Seq &epoch, const Clock::time_point &t);

    /** @brief Append a timestamp delta control message to a packet */
    void appendTimestampDelta(NodeId node_id,
                              const Seq &epoch,
                              const Clock::time_point &delta);

    /** @brief Return iterator to beginning control data. */
    iterator begin() const
    {
        return iterator(*this);
    }

    /** @brief Return iterator to end of control data. */
    iterator end() const
    {
        return iterator(*this, 0);
    }

    /** @brief Return true if this is an IP packet, false otherwise */
    bool isIP(void);

    /** @brief Return true if this is an IP packet of the specified IP protocol,
     * false otherwise
     */
    bool isIPProto(uint8_t proto);

    /** @brief Return true if this is a TCP packet, false otherwise */
    bool isTCP(void)
    {
        return isIPProto(IPPROTO_TCP);
    }

    /** @brief Return true if this is a UDP packet, false otherwise */
    bool isUDP(void)
    {
        return isIPProto(IPPROTO_UDP);
    }
};

/** @brief A packet received from the network. */
struct NetPacket : public Packet
{
    NetPacket(size_t n) : Packet(n), tx_params(nullptr) {};

    /** @brief TX parameters */
    TXParams *tx_params;

    /** @brief Multiplicative TX gain. */
    float g;
};

/** @brief A packet received from the radio. */
struct RadioPacket : public Packet
{
    RadioPacket() : Packet(), barrier(false) {};
    RadioPacket(unsigned char* data, size_t n) : Packet(data, n), barrier(false) {}

    /** @brief Error vector magnitude [dB] */
    float evm;

    /** @brief Received signal strength indicator [dB] */
    float rssi;

    /** @brief Carrier frequency offset (f/Fs) */
    float cfo;

    /** @brief This Boolean is true if this packet is a barrier and should not
     * be processed or removed from a queue except by its creator.
     */
    bool barrier;
};

/** @brief Compute the size of the specified control message. */
constexpr size_t ctrlsize(ControlMsg::Type ty)
{
    switch (ty) {
        case ControlMsg::kHello:
            return offsetof(ControlMsg, hello) + sizeof(ControlMsg::Hello);

        case ControlMsg::kTimestamp:
            return offsetof(ControlMsg, timestamp) + sizeof(ControlMsg::Timestamp);

        case ControlMsg::kTimestampDelta:
            return offsetof(ControlMsg, timestamp_delta) + sizeof(ControlMsg::TimestampDelta);

        default:
            return 0;
    }
}

#endif /* PACKET_HH_ */
