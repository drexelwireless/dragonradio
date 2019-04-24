#ifndef PACKET_HH_
#define PACKET_HH_

#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>

#include <complex>
#include <cstddef>
#include <iterator>
#include <optional>
#include <vector>

#include <liquid/liquid.h>

#include "buffer.hh"
#include "Clock.hh"
#include "Header.hh"
#include "net/mgen.h"
#include "phy/Channel.hh"
#include "phy/TXParams.hh"

/** @brief A time */
struct Time {
    uint64_t secs;
    double frac_secs;

    void from_mono_time(MonoClock::time_point t)
    {
        secs = t.t.get_full_secs();
        frac_secs = t.t.get_frac_secs();
    }

    MonoClock::time_point to_mono_time(void) const
    {
        return MonoClock::time_point { uhd::time_spec_t { static_cast<time_t>(secs), frac_secs } };
    }
};

/** @brief A Control message */
struct ControlMsg {
    enum Type {
        kHello,
        kTimestamp,
        kTimestampEcho,
        kNak,
        kSelectiveAck,
    };

    struct Hello {
        bool is_gateway;
    };

    struct Timestamp {
        /** @brief Transmission time of this packet at the transmitter */
        Time t_sent;
    };

    struct TimestampEcho {
        /** @brief Node ID of original timestamp transmitter */
        NodeId node;
        /** @brief Transmitter's timestamp on sent packet */
        Time t_sent;
        /** @brief Receiver's timestamp of packet */
        Time t_recv;
    };

    struct SelectiveAck {
        Seq begin;
        Seq end;
    };

    using Nak = Seq;

    Type type;

    union {
        Hello hello;
        Timestamp timestamp;
        TimestampEcho timestamp_echo;
        Nak nak;
        SelectiveAck ack;
    };
};

/** @brief A flow UID. */
typedef uint16_t FlowUID;

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

    /** @brief Flow UID */
    std::optional<FlowUID> flow_uid;

    /** @brief Packet timestamp */
    MonoClock::time_point timestamp;

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

    /** @brief Get length of control info */
    uint16_t getControlLen(void) const;

    /** @brief Set length of control info */
    void setControlLen(uint16_t len);

    /** @brief Clear control messages contained in packet */
    void clearControl(void);

    /** @brief Append a control message to a packet */
    void appendControl(const ControlMsg &ctrl);

    /** @brief Append a Hello control message to a packet */
    void appendHello(const ControlMsg::Hello &hello);

    /** @brief Append a Timestamp control message to a packet */
    void appendTimestamp(const MonoClock::time_point &t_sent);

    /** @brief Append a Timestamp Echo control message to a packet */
    void appendTimestampEcho(NodeId node_id,
                             const MonoClock::time_point &t_sent,
                             const MonoClock::time_point &t_recv);

    /** @brief Append a NAK control message to a packet */
    void appendNak(const Seq &seq);

    /** @brief Append a selective ACK control message to a packet */
    void appendSelectiveAck(const Seq &begin, const Seq &end);

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

    /** @brief Get Ethernet header
     * @return A pointer to the Ethernet header or nullptr if this is not an
     * Ethernet packet
     */
    const struct ether_header *getEthernetHdr(void) const
    {
        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header))
            return nullptr;

        const struct ether_header *eth = reinterpret_cast<const struct ether_header*>(data() + sizeof(ExtendedHeader));

        if (ntohs(eth->ether_type) != ETHERTYPE_IP)
            return nullptr;

        return eth;
    }

    /** @brief Get IP header
     * @pram ip_p A pointer to a variable that will hold the IP protocol
     * contained in the header
     * @return A pointer to the IP header or nullptr if this is not an IP packet
     */
    const struct ip *getIPHdr(uint8_t *ip_p) const
    {
        if (!getEthernetHdr())
            return nullptr;

        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + sizeof(struct ip))
            return nullptr;

        const struct ip *iph = reinterpret_cast<const struct ip*>(data() + sizeof(ExtendedHeader) + sizeof(struct ether_header));

        std::memcpy(ip_p, reinterpret_cast<const char*>(iph) + offsetof(struct ip, ip_p), sizeof(*ip_p));

        return iph;
    }

    /** @brief Get UDP header
     * @return A pointer to the UDP header or nullptr if this is not a UDP
     * packet
     */
    const struct udphdr *getUDPHdr(void) const
    {
        const struct ip *iph;
        uint8_t         ip_p;

        iph = getIPHdr(&ip_p);
        if (!iph || ip_p != IPPROTO_UDP)
            return nullptr;

        size_t ip_hl = iph->ip_hl*4;

        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct udphdr))
            return nullptr;

        return reinterpret_cast<const struct udphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);
    }

    /** @brief Get TCP header
     * @return A pointer to the TCP header or nullptr if this is not a TCP
     * packet
     */
    const struct tcphdr *getTCPHdr(void) const
    {
        const struct ip *iph;
        uint8_t         ip_p;

        iph = getIPHdr(&ip_p);
        if (!iph || ip_p != IPPROTO_TCP)
            return nullptr;

        size_t ip_hl = iph->ip_hl*4;

        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct tcphdr))
            return nullptr;

        return reinterpret_cast<const struct tcphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);
    }

    /** @brief Get MGEN header
     * @return A pointer to the MGEN header or nullptr if this is not a MGEN
     * packet
     */
    const struct mgenhdr *getMGENHdr(void) const;

    /** @brief Get payload size
     * @return The size of the data portion of a UDP or TCP packet.
     */
    size_t getPayloadSize(void) const;

    /** @brief Return true if this is an IP packet, false otherwise */
    bool isIP(void) const
    {
        uint8_t ip_p;

        return getIPHdr(&ip_p) != nullptr;
    }

    /** @brief Return true if this is an IP packet of the specified IP protocol,
     * false otherwise
     */
    bool isIPProto(uint8_t proto) const
    {
        uint8_t ip_p;

        return getIPHdr(&ip_p) != nullptr && ip_p == proto;
    }

    /** @brief Return true if this is a TCP packet, false otherwise */
    bool isTCP(void) const
    {
        return isIPProto(IPPROTO_TCP);
    }

    /** @brief Return true if this is a UDP packet, false otherwise */
    bool isUDP(void) const
    {
        return isIPProto(IPPROTO_UDP);
    }
};

/** @brief A packet received from the network. */
struct NetPacket : public Packet
{
    NetPacket(size_t n) : Packet(n), tx_params(nullptr) {};

    /** @brief Packet delivery deadline */
    std::optional<MonoClock::time_point> deadline;

    /** @brief TX parameters */
    TXParams *tx_params;

    /** @brief Multiplicative TX gain. */
    float g;

    /** @brief Return true if the packet's deadline has passed, false otherwise */
    bool deadlinePassed(const MonoClock::time_point &now)
    {
        return deadline && *deadline < now;
    }

    /** @brief Return true if this packet should be dropped, false otherwise */
    bool shouldDrop(const MonoClock::time_point &now)
    {
        return !isFlagSet(kSYN) && deadlinePassed(now);
    }
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

    /** @brief Channel the packet was received on */
    Channel channel;

    /** @brief MCS used for this packet by transmitter */
    MCS mcs;

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

        case ControlMsg::kTimestampEcho:
            return offsetof(ControlMsg, timestamp_echo) + sizeof(ControlMsg::TimestampEcho);

        case ControlMsg::kNak:
            return offsetof(ControlMsg, nak) + sizeof(ControlMsg::Nak);

        case ControlMsg::kSelectiveAck:
            return offsetof(ControlMsg, ack) + sizeof(ControlMsg::SelectiveAck);

        default:
            return 0;
    }
}

#endif /* PACKET_HH_ */
