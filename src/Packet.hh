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
#include "phy/Modem.hh"

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
        kReceiverStats,
        kNak,
        kSelectiveAck,
        kSetUnack
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
    } __attribute__((packed));

    struct ReceiverStats {
        /** @brief Long-term EVM at receiver */
        double long_evm;

        /** @brief Long-term RSSI at receiver */
        double long_rssi;
    };

    using Nak = Seq;

    struct SelectiveAck {
        Seq begin;
        Seq end;
    };

    struct SetUnack {
        /** @brief Sender's first un-ACK'ed packet */
        Seq unack;
    };

    uint8_t type;

    union {
        Hello hello;
        Timestamp timestamp;
        TimestampEcho timestamp_echo;
        ReceiverStats receiver_stats;
        Nak nak;
        SelectiveAck ack;
        SetUnack unack;
    };
} __attribute__((packed));

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

    Packet() = delete;

    Packet(const Header &hdr_)
      : buffer()
      , hdr(hdr_)
      , payload_size(0)
      , internal_flags({0})
    {
    }

    explicit Packet(size_t n)
      : buffer(n)
      , hdr({0})
      , payload_size(0)
      , internal_flags({0})
    {
        assert(n >= sizeof(ExtendedHeader));
    }

    Packet(const Header &hdr_, unsigned char* data, size_t n)
      : buffer(data, n)
      , hdr(hdr_)
      , payload_size(0)
      , internal_flags({0})
    {
        assert(n >= sizeof(ExtendedHeader));
    }

    /** @brief Header */
    Header hdr;

    /** @brief Flow UID */
    std::optional<FlowUID> flow_uid;

    /** @brief Packet timestamp */
    MonoClock::time_point timestamp;

    /** @brief Payload size
     * @return The size of the data portion of a UDP or TCP packet.
     */
    size_t payload_size;

    /** @brief Internal flags */
    struct InternalFlags {
        /** @brief Set if the packet had invalid header */
        uint8_t invalid_header : 1;

        /** @brief Set if the packet had invalid payload */
        uint8_t invalid_payload : 1;

        /** @brief Set if the packet is a retransmission */
        uint8_t retransmission : 1;

        /** @brief Set if the packet has an assigned sequence number */
        uint8_t has_seq : 1;

        /** @brief Set if the packet contains a selective ACK */
        uint8_t has_selective_ack : 1;

        /** @brief Set if this packet should be timestamped */
        uint8_t timestamp : 1;
    } internal_flags;

    /** @brief Get extended header */
    ExtendedHeader &ehdr(void)
    {
        assert(size() >= sizeof(ExtendedHeader));
        return *reinterpret_cast<ExtendedHeader*>(data());
    }

    /** @brief Get extended header */
    const ExtendedHeader &ehdr(void) const
    {
        assert(size() >= sizeof(ExtendedHeader));
        return *reinterpret_cast<const ExtendedHeader*>(data());
    }

    /** @brief Check packet integrity */
    bool integrityIntact(void) const
    {
        if (size() < sizeof(ExtendedHeader))
            return false;

        if (hdr.flags.has_control) {
            if (size() < sizeof(ExtendedHeader) + ehdr().data_len + sizeof(uint16_t))
                return false;

            return size() == sizeof(ExtendedHeader) + ehdr().data_len + sizeof(uint16_t) + getControlLen();
        } else
            return size() == sizeof(ExtendedHeader) + ehdr().data_len;
    }

    /** @brief Get length of control info */
    uint16_t getControlLen(void) const;

    /** @brief Set length of control info */
    void setControlLen(uint16_t ctrl_len);

    /** @brief Clear control messages contained in packet */
    void clearControl(void)
    {
        hdr.flags.has_control = 0;
        resize(sizeof(ExtendedHeader) + ehdr().data_len);
    }

    /** @brief Append a control message to a packet */
    void appendControl(const ControlMsg &ctrl);

    /** @brief Remove last control message from a packet */
    /** Blindly remove the last control message from a packet, assuming in has
     * type type.
     */
    void removeLastControl(ControlMsg::Type type);

    /** @brief Append a Hello control message to a packet */
    void appendHello(const ControlMsg::Hello &hello);

    /** @brief Append a Timestamp control message to a packet */
    void appendTimestamp(const MonoClock::time_point &t_sent);

    /** @brief Remove a Timestamp control message from the end of a packet */
    void removeTimestamp(void)
    {
        removeLastControl(ControlMsg::Type::kTimestamp);
    }

    /** @brief Append a Timestamp Echo control message to a packet */
    void appendTimestampEcho(NodeId node_id,
                             const MonoClock::time_point &t_sent,
                             const MonoClock::time_point &t_recv);

    /** @brief Append receiver statistics control message to a packet */
    void appendReceiverStats(double long_evm, double long_rssi);

    /** @brief Append a NAK control message to a packet */
    void appendNak(const Seq &seq);

    /** @brief Append a selective ACK control message to a packet */
    void appendSelectiveAck(const Seq &begin, const Seq &end);

    /** @brief Append a "set unack" control message to a packet */
    void appendSetUnack(const Seq &unack);

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

        return reinterpret_cast<const struct ether_header*>(data() + sizeof(ExtendedHeader));
    }

    /** @brief Get IP header
     * @return A pointer to the IP header or nullptr if this is not an IP packet
     */
    const struct ip *getIPHdr(void) const
    {
        const struct ether_header *eth = getEthernetHdr();

        if (!eth || ntohs(eth->ether_type) != ETHERTYPE_IP)
            return nullptr;

        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + sizeof(struct ip))
            return nullptr;

        return reinterpret_cast<const struct ip*>(data() + sizeof(ExtendedHeader) + sizeof(struct ether_header));
    }

    /** @brief Get IP header
     * @return A pointer to the IP header or nullptr if this is not an IP packet
     */
    struct ip *getIPHdr(void)
    {
        return const_cast<struct ip*>(static_cast<const Packet &>(*this).getIPHdr());
    }

    /** @brief Get UDP header
     * @return A pointer to the UDP header or nullptr if this is not a UDP
     * packet
     */
    const struct udphdr *getUDPHdr(void) const
    {
        const struct ip *iph = getIPHdr();

        if (!iph || iph->ip_p != IPPROTO_UDP)
            return nullptr;

        size_t ip_hl = iph->ip_hl*4;

        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct udphdr))
            return nullptr;

        return reinterpret_cast<const struct udphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);
    }

    /** @brief Get UDP header
     * @return A pointer to the UDP header or nullptr if this is not a UDP
     * packet
     */
    struct udphdr *getUDPHdr(void)
    {
        return const_cast<struct udphdr*>(static_cast<const Packet &>(*this).getUDPHdr());
    }

    /** @brief Get TCP header
     * @return A pointer to the TCP header or nullptr if this is not a TCP
     * packet
     */
    const struct tcphdr *getTCPHdr(void) const
    {
        const struct ip *iph = getIPHdr();

        if (!iph || iph->ip_p != IPPROTO_TCP)
            return nullptr;

        size_t ip_hl = iph->ip_hl*4;

        if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct tcphdr))
            return nullptr;

        return reinterpret_cast<const struct tcphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);
    }

    /** @brief Get TCP header
     * @return A pointer to the TCP header or nullptr if this is not a TCP
     * packet
     */
    struct tcphdr *getTCPHdr(void)
    {
        return const_cast<struct tcphdr*>(static_cast<const Packet &>(*this).getTCPHdr());
    }

    /** @brief Get MGEN header
     * @return A pointer to the MGEN header or nullptr if this is not a MGEN
     * packet
     */
    const struct mgenhdr *getMGENHdr(void) const;

    /** @brief Get MGEN header
     * @return A pointer to the MGEN header or nullptr if this is not a MGEN
     * packet
     */
    struct mgenhdr *getMGENHdr(void)
    {
        return const_cast<struct mgenhdr*>(static_cast<const Packet &>(*this).getMGENHdr());
    }

    /** @brief Get payload size
     * @return The size of the data portion of a UDP or TCP packet.
     */
    size_t getPayloadSize(void) const;

    /** @brief Return true if this is an IP packet, false otherwise */
    bool isIP(void) const
    {
        return getIPHdr() != nullptr;
    }

    /** @brief Return true if this is an IP packet of the specified IP protocol,
     * false otherwise
     */
    bool isIPProto(uint8_t proto) const
    {
        const struct ip *iph = getIPHdr();

        return iph != nullptr && iph->ip_p == proto;
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
    NetPacket() = delete;

    explicit NetPacket(size_t n)
      : Packet(n)
      , mcsidx(0)
      , g(1.0)
    {
    }

    /** @brief Packet delivery deadline */
    std::optional<MonoClock::time_point> deadline;

    /** @brief MCS to use, given as an index into PHY's MCS table */
    mcsidx_t mcsidx;

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
        return !hdr.flags.syn && deadlinePassed(now);
    }
};

/** @brief A packet received from the radio. */
struct RadioPacket : public Packet
{
    RadioPacket() = delete;

    RadioPacket(const Header &hdr)
      : Packet(hdr)
    {
    }

    RadioPacket(const Header &hdr, unsigned char* data, size_t n)
      : Packet(hdr, data, n)
    {
    }

    /** @brief Error vector magnitude [dB] */
    float evm;

    /** @brief Received signal strength indicator [dB] */
    float rssi;

    /** @brief Carrier frequency offset (f/Fs) */
    float cfo;

    /** @brief Channel the packet was received on */
    Channel channel;
};

/** @brief Compute the size of the specified control message. */
constexpr size_t ctrlsize(uint8_t ty)
{
    switch (ty) {
        case ControlMsg::kHello:
            return offsetof(ControlMsg, hello) + sizeof(ControlMsg::Hello);

        case ControlMsg::kTimestamp:
            return offsetof(ControlMsg, timestamp) + sizeof(ControlMsg::Timestamp);

        case ControlMsg::kTimestampEcho:
            return offsetof(ControlMsg, timestamp_echo) + sizeof(ControlMsg::TimestampEcho);

        case ControlMsg::kReceiverStats:
            return offsetof(ControlMsg, receiver_stats) + sizeof(ControlMsg::ReceiverStats);

        case ControlMsg::kNak:
            return offsetof(ControlMsg, nak) + sizeof(ControlMsg::Nak);

        case ControlMsg::kSelectiveAck:
            return offsetof(ControlMsg, ack) + sizeof(ControlMsg::SelectiveAck);

        case ControlMsg::kSetUnack:
            return offsetof(ControlMsg, unack) + sizeof(ControlMsg::SetUnack);

        default:
            return 0;
    }
}

static_assert(ctrlsize(ControlMsg::kHello) == 2);
static_assert(ctrlsize(ControlMsg::kTimestamp) == 17);
static_assert(ctrlsize(ControlMsg::kTimestampEcho) == 34);
static_assert(ctrlsize(ControlMsg::kReceiverStats) == 17);
static_assert(ctrlsize(ControlMsg::kNak) == 3);
static_assert(ctrlsize(ControlMsg::kSelectiveAck) == 5);
static_assert(ctrlsize(ControlMsg::kSetUnack) == 3);

#endif /* PACKET_HH_ */
