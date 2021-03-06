// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Packet.hh"

Packet::iterator::iterator(const Packet &pkt)
  : pkt_(pkt)
  , ctrl_()
{
    // Make sure we have control data
    if (!pkt_.hdr.flags.has_control || pkt_.size() < sizeof(ExtendedHeader) + pkt_.ehdr().data_len + sizeof(ctrl_len_)) {
        ctrl_len_ = 0;
        ctrl_ptr_ = nullptr;
        return;
    }

    // Extract the size of available control data
    memcpy(&ctrl_len_, pkt_.data() + sizeof(ExtendedHeader) + pkt_.ehdr().data_len, sizeof(ctrl_len_));
    ctrl_len_ = std::min(ctrl_len_, static_cast<uint16_t>(pkt_.size() - sizeof(ExtendedHeader) - pkt_.ehdr().data_len));
    ctrl_ptr_ = pkt_.data() + sizeof(ExtendedHeader) + pkt_.ehdr().data_len + sizeof(ctrl_len_);

    // Try to get the first control message
    (*this)++;
}

Packet::iterator::iterator(const Packet &pkt, int)
  : pkt_(pkt)
  , ctrl_len_(0)
  , ctrl_ptr_(nullptr)
  , ctrl_()
{
}

bool Packet::iterator::operator ==(const iterator& other)
{
    return &this->pkt_ == &other.pkt_ &&
        this->ctrl_len_ == other.ctrl_len_ &&
        this->ctrl_ptr_ == other.ctrl_ptr_;
}

bool Packet::iterator::operator !=(const iterator& other)
{
    return !(*this == other);
}

Packet::iterator& Packet::iterator::operator ++()
{
    if (ctrl_len_ < sizeof(ControlMsg::type)) {
        ctrl_len_ = 0;
        ctrl_ptr_ = nullptr;
        return *this;
    }

    memcpy(&ctrl_.type, ctrl_ptr_, sizeof(ControlMsg::type));

    if (ctrl_len_ < ctrlsize(ctrl_.type)) {
        ctrl_len_ = 0;
        ctrl_ptr_ = nullptr;
        return *this;
    }

    memcpy(&ctrl_, ctrl_ptr_, ctrlsize(ctrl_.type));
    ctrl_len_ -= ctrlsize(ctrl_.type);
    ctrl_ptr_ += ctrlsize(ctrl_.type);

    return *this;
}

Packet::iterator& Packet::iterator::operator ++(int)
{
    return ++(*this);
}

const ControlMsg &Packet::iterator::operator *()
{
    return ctrl_;
}

const ControlMsg *Packet::iterator::operator ->()
{
    return &ctrl_;
}

uint16_t Packet::getControlLen(void) const
{
    uint16_t ctrl_len;

    if (!hdr.flags.has_control)
        return 0;

    assert(size() >= sizeof(ExtendedHeader) + ehdr().data_len + sizeof(uint16_t));

    memcpy(&ctrl_len, data() + sizeof(ExtendedHeader) + ehdr().data_len, sizeof(uint16_t));

    assert(size() == sizeof(ExtendedHeader) + ehdr().data_len + sizeof(uint16_t) + ctrl_len);

    return ctrl_len;
}

void Packet::setControlLen(uint16_t ctrl_len)
{
    if (!hdr.flags.has_control) {
        hdr.flags.has_control = 1;
        resize(size() + sizeof(uint16_t));
    }

    memcpy(&(*this)[sizeof(ExtendedHeader) + ehdr().data_len], &ctrl_len, sizeof(uint16_t));
}

void Packet::appendControl(const ControlMsg &ctrl)
{
    uint16_t ctrl_len = getControlLen();

    // Increase length of control information
    setControlLen(ctrl_len + ctrlsize(ctrl.type));

    // Add space for control data
    resize(size() + ctrlsize(ctrl.type));

    // Copy control data to packet
    memcpy(&(*this)[sizeof(ExtendedHeader) + ehdr().data_len + sizeof(uint16_t) + ctrl_len], &ctrl, ctrlsize(ctrl.type));
}

void Packet::removeLastControl(ControlMsg::Type type)
{
    uint16_t ctrl_len = getControlLen();

    // Decrease length of control information
    setControlLen(ctrl_len - ctrlsize(type));

    // Remove space for control data
    resize(size() - ctrlsize(type));
}

void Packet::appendHello(const ControlMsg::Hello &hello)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kHello;
    msg.hello = hello;

    appendControl(msg);
}

void Packet::appendTimestampSent(TimestampSeq tseq,
                                 const MonoClock::time_point &t_sent)
{
    ControlMsg msg;
    Time temp;

    temp.from_mono_time(t_sent);

    msg.type = ControlMsg::Type::kTimestampSent;
    msg.timestamp_sent.tseq = tseq;
    msg.timestamp_sent.t_sent = temp;

    appendControl(msg);
}

void Packet::appendTimestampRecv(NodeId node_id,
                                 TimestampSeq tseq,
                                 const MonoClock::time_point &t_recv)
{
    ControlMsg msg;
    Time temp;

    temp.from_mono_time(t_recv);

    msg.type = ControlMsg::Type::kTimestampRecv;
    msg.timestamp_recv.node = node_id;
    msg.timestamp_recv.tseq = tseq;
    msg.timestamp_recv.t_recv = temp;

    appendControl(msg);
}

void Packet::appendReceiverStats(float long_evm, float long_rssi)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kReceiverStats;
    msg.receiver_stats.long_evm = long_evm;
    msg.receiver_stats.long_rssi = long_rssi;

    appendControl(msg);
}

void Packet::appendNak(const Seq &seq)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kNak;
    msg.nak = seq;

    appendControl(msg);
}

void Packet::appendSelectiveAck(const Seq &begin, const Seq &end)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kSelectiveAck;
    msg.ack.begin = begin;
    msg.ack.end = end;

    appendControl(msg);
}

void Packet::appendSetUnack(const Seq &unack)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kSetUnack;
    msg.unack.unack = unack;

    appendControl(msg);
}

const struct mgenhdr *Packet::getMGENHdr(void) const
{
    const struct ip *iph = getIPHdr();

    if (!iph)
       return nullptr;

    size_t ip_hl = iph->ip_hl*4;

    const struct mgenhdr *mgenh = nullptr;

    switch (iph->ip_p) {
        case IPPROTO_UDP:
        {
           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct udphdr) + sizeof(struct mgenhdr))
               return nullptr;

            mgenh = reinterpret_cast<const struct mgenhdr*>(reinterpret_cast<const char*>(iph) + ip_hl + sizeof(struct udphdr));
        }
        break;

        case IPPROTO_TCP:
        {
           const struct tcphdr *tcph;
           size_t tcp_hl;

           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct tcphdr))
               return nullptr;

           tcph = reinterpret_cast<const struct tcphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);
           tcp_hl = tcph->th_off*4;

           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + tcp_hl + sizeof(struct mgenhdr))
               return nullptr;

           mgenh = reinterpret_cast<const struct mgenhdr*>(reinterpret_cast<const char*>(iph) + ip_hl + tcp_hl);
        }
        break;
    }

    if (mgenh) {
        uint16_t messageSize;

        // Make sure the MGEN-specified data length and version are correct
        std::memcpy(&messageSize, reinterpret_cast<const uint16_t*>(mgenh) + offsetof(struct mgenhdr, messageSize), sizeof(messageSize));

        if (ntohs(messageSize) == payload_size &&
            (mgenh->version == MGEN_VERSION || mgenh->version == DARPA_MGEN_VERSION))
            return mgenh;
        else
            return nullptr;
    } else
        return nullptr;
}

size_t Packet::getPayloadSize(void) const
{
    const struct ip *iph = getIPHdr();

    if (!iph)
       return 0;

    size_t ip_hl = iph->ip_hl*4;

    switch (iph->ip_p) {
        case IPPROTO_UDP:
        {
            if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct udphdr))
               return 0;

            const struct udphdr *udph = reinterpret_cast<const struct udphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);

            return ntohs(udph->uh_ulen) - sizeof(struct udphdr);
        }
        break;

        case IPPROTO_TCP:
        {
           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct tcphdr))
               return 0;

           const struct tcphdr *tcph = reinterpret_cast<const struct tcphdr*>(reinterpret_cast<const char*>(iph) + ip_hl);
           size_t tcp_hl = tcph->th_off*4;

           return size() - (sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + tcp_hl);
        }
        break;
    }

    return 0;
}

void Packet::initMGENInfo(void)
{
    if (hdr.flags.compressed) {
        unsigned off = sizeof(ExtendedHeader);

        // Get compression flags
        CompressionFlags flags;

        if (off + sizeof(CompressionFlags) > size())
            return;

        memcpy(&flags, data() + off, sizeof(CompressionFlags));
        off += sizeof(CompressionFlags);

        if (flags.type == kMGEN || flags.type == kDARPAMGEN) {
            //
            // IP header
            //

            off += sizeof(u_int8_t); // TOS
            off += sizeof(u_short); // IP id
            if (flags.read_ttl)
                off += sizeof(uint8_t); // TTL

            // Skip IP source and destination addresses
            switch (flags.ipaddr_type) {
                case kIPUncompressed:
                    off += 2*sizeof(in_addr_t);
                    break;

                case kIPExternal:
                    off += 2*sizeof(uint8_t);
                    break;
            }

            //
            // UDP header
            //

            off += sizeof(u_int16_t); // Source port

            // UDP destination port is also the flow id.
            u_int16_t dport;

            if (off + sizeof(u_int16_t) > size())
                return;

            memcpy(&dport, data() + off, sizeof(u_int16_t));
            off += sizeof(u_int16_t);

            flow_uid = mgen_flow_uid = ntohs(dport);

            //
            // MGEN header
            //

            // MGEN sequence number is next
            uint32_t seqno;

            if (off + sizeof(uint32_t) > size())
                return;

            memcpy(&seqno, data() + off, sizeof(uint32_t));
            off += sizeof(uint32_t);

            mgen_seqno = ntohl(seqno);

            // Skip reserved field
            if (flags.type == kDARPAMGEN)
                off += sizeof(uint32_t);

            // Read timestamp
            uint32_t mgen_secs;
            uint32_t mgen_usecs;
            int64_t  secs;
            int32_t  usecs;

            memcpy(&mgen_secs, data() + off, sizeof(uint32_t));
            off += sizeof(uint32_t);

            memcpy(&mgen_usecs, data() + off, sizeof(uint32_t));
            off += sizeof(uint32_t);

            secs = ntohl(mgen_secs);
            usecs = ntohl(mgen_usecs);

            wall_timestamp = WallClock::time_point{static_cast<int64_t>(secs), usecs/1e6};
        }
    } else {
         const struct mgenhdr *mgenh = getMGENHdr();

         if (mgenh) {
             flow_uid = mgen_flow_uid = mgenh->getFlowId();
             mgen_seqno = mgenh->getSequenceNumber();
             wall_timestamp = mgenh->getTimestamp();
         }
    }
}

void NetPacket::appendTimestamp(TimestampSeq tseq)
{
    timestamp_seq = tseq;

    ControlMsg msg;

    msg.type = ControlMsg::Type::kTimestamp;
    msg.timestamp.tseq = tseq;

    appendControl(msg);
}
