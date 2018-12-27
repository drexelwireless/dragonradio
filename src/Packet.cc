#include "Packet.hh"

Packet::iterator::iterator(const Packet &pkt)
  : pkt_(pkt)
  , ctrl_()
{
    // Make sure we have control data
    if (!pkt_.isFlagSet(kControl) || pkt_.size() < sizeof(ExtendedHeader) + pkt_.data_len + sizeof(ctrl_len_)) {
        ctrl_len_ = 0;
        ctrl_ptr_ = nullptr;
        return;
    }

    // Extract the size of available control data
    memcpy(&ctrl_len_, pkt_.data() + sizeof(ExtendedHeader) + pkt_.data_len, sizeof(ctrl_len_));
    ctrl_len_ = std::min(ctrl_len_, static_cast<uint16_t>(pkt_.size() - sizeof(ExtendedHeader) - pkt_.data_len));
    ctrl_ptr_ = pkt_.data() + sizeof(ExtendedHeader) + pkt_.data_len + sizeof(ctrl_len_);

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
    if (ctrl_len_ < sizeof(ControlMsg::Type)) {
        ctrl_len_ = 0;
        ctrl_ptr_ = nullptr;
        return *this;
    }

    memcpy(&ctrl_.type, ctrl_ptr_, sizeof(ControlMsg::Type));

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

void Packet::clearControl(void)
{
    clearFlag(kControl);
    resize(sizeof(ExtendedHeader) + data_len);
}

void Packet::appendControl(const ControlMsg &ctrl)
{
    uint16_t ctrl_len = 0;

    if (!isFlagSet(kControl)) {
        setFlag(kControl);
        resize(size() + sizeof(uint16_t) + ctrlsize(ctrl.type));
    } else {
        memcpy(&ctrl_len, &(*this)[sizeof(ExtendedHeader) + data_len], sizeof(uint16_t));
        resize(size() + ctrlsize(ctrl.type));
    }

    memcpy(&(*this)[sizeof(ExtendedHeader) + data_len + sizeof(uint16_t) + ctrl_len], &ctrl, ctrlsize(ctrl.type));

    ctrl_len += ctrlsize(ctrl.type);
    memcpy(&(*this)[sizeof(ExtendedHeader) + data_len], &ctrl_len, sizeof(uint16_t));
}

void Packet::appendHello(const ControlMsg::Hello &hello)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kHello;
    msg.hello = hello;

    appendControl(msg);
}

void Packet::appendTimestamp(const MonoClock::time_point &t_sent)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kTimestamp;
    msg.timestamp.t_sent.from_mono_time(t_sent);

    appendControl(msg);
}

void Packet::appendTimestampEcho(NodeId node_id,
                                const MonoClock::time_point &t_sent,
                                const MonoClock::time_point &t_recv)
{
    ControlMsg msg;

    msg.type = ControlMsg::Type::kTimestampEcho;
    msg.timestamp_echo.node = node_id;
    msg.timestamp_echo.t_sent.from_mono_time(t_sent);
    msg.timestamp_echo.t_recv.from_mono_time(t_recv);

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

struct mgenhdr *Packet::getMGENHdr(void)
{
    struct ip *iph;
    uint8_t   ip_p;

    iph = getIPHdr(&ip_p);
    if (!iph)
       return nullptr;

    size_t ip_hl = iph->ip_hl*4;

    struct mgenhdr *mgenh = nullptr;

    switch (ip_p) {
        case IPPROTO_UDP:
        {
           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct udphdr) + sizeof(struct mgenhdr))
               return nullptr;

            mgenh = reinterpret_cast<struct mgenhdr*>(reinterpret_cast<char*>(iph) + ip_hl + sizeof(struct udphdr));
        }
        break;

        case IPPROTO_TCP:
        {
           struct tcphdr *tcph;
           size_t tcp_hl;

           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct tcphdr))
               return nullptr;

           tcph = reinterpret_cast<struct tcphdr*>(reinterpret_cast<char*>(iph) + ip_hl);
           tcp_hl = tcph->th_off*4;

           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + tcp_hl + sizeof(struct mgenhdr))
               return nullptr;

           mgenh = reinterpret_cast<struct mgenhdr*>(reinterpret_cast<char*>(iph) + ip_hl + tcp_hl);
        }
        break;
    }

    if (mgenh) {
        uint16_t messageSize;

        // Make sure the MGEN-specified data length and version are correct
        std::memcpy(&messageSize, reinterpret_cast<uint16_t*>(mgenh) + offsetof(struct mgenhdr, messageSize), sizeof(messageSize));

        if (ntohs(messageSize) == getPayloadSize() &&
            (mgenh->version == MGEN_VERSION || mgenh->version == DARPA_MGEN_VERSION))
            return mgenh;
        else
            return nullptr;
    } else
        return nullptr;
}

size_t Packet::getPayloadSize(void)
{
    struct ip *iph;
    uint8_t   ip_p;

    iph = getIPHdr(&ip_p);
    if (!iph)
       return 0;

    size_t ip_hl = iph->ip_hl*4;

    switch (ip_p) {
        case IPPROTO_UDP:
        {
            if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct udphdr))
               return 0;

            struct udphdr *udph = reinterpret_cast<struct udphdr*>(reinterpret_cast<char*>(iph) + ip_hl);

            return ntohs(udph->uh_ulen) - sizeof(struct udphdr);
        }
        break;

        case IPPROTO_TCP:
        {
           if (size() < sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + sizeof(struct tcphdr))
               return 0;

           struct tcphdr *tcph = reinterpret_cast<struct tcphdr*>(reinterpret_cast<char*>(iph) + ip_hl);
           size_t tcp_hl = tcph->th_off*4;

           return size() - (sizeof(ExtendedHeader) + sizeof(struct ether_header) + ip_hl + tcp_hl);
        }
        break;
    }

    return 0;
}
