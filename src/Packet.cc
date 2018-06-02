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

void Packet::appendControl(ControlMsg &ctrl)
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