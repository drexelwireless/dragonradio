#include "RadioPacketQueue.hh"

RadioPacketQueue::RadioPacketQueue() :
    _done(false)
{
}

RadioPacketQueue::~RadioPacketQueue()
{
}

void RadioPacketQueue::push(std::unique_ptr<RadioPacket> pkt)
{
    {
        std::lock_guard<std::mutex> lock(_m);

        _q.emplace_back(std::move(pkt));
    }

    _cond.notify_one();
}

void RadioPacketQueue::push(RadioPacketQueue::barrier b,
                            std::unique_ptr<RadioPacket> pkt)
{
    {
        std::lock_guard<std::mutex> lock(_m);

        _q.emplace(b, std::move(pkt));
    }

    _cond.notify_one();
}

RadioPacketQueue::barrier RadioPacketQueue::push_barrier(void)
{
    auto pkt = std::make_unique<RadioPacket>();

    pkt->barrier = true;

    RadioPacketQueue::barrier b;

    {
        std::lock_guard<std::mutex> lock(_m);

        b = _q.emplace(_q.end(), std::move(pkt));
    }

    _cond.notify_one();

    return b;
}

void RadioPacketQueue::erase_barrier(RadioPacketQueue::barrier b)
{
    {
        std::lock_guard<std::mutex> lock(_m);

        _q.erase(b);
    }

    _cond.notify_all();
}

bool RadioPacketQueue::pop(std::unique_ptr<RadioPacket>& pkt)
{
    std::unique_lock<std::mutex> lock(_m);

    _cond.wait(lock, [this]{ return _done || (!_q.empty() && !_q.front()->barrier); });
    if (_done)
        return false;
    else {
        pkt = std::move(_q.front());
        _q.pop_front();
        return true;
    }
}

void RadioPacketQueue::stop(void)
{
    _done = true;
    _cond.notify_all();
}
