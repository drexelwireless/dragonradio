#include "phy/RadioPacketQueue.hh"

RadioPacketQueue::RadioPacketQueue() :
    done_(false)
{
}

RadioPacketQueue::~RadioPacketQueue()
{
    stop();
}

void RadioPacketQueue::push(std::unique_ptr<RadioPacket> pkt)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace_back(std::move(pkt));
    }

    cond_.notify_one();
}

void RadioPacketQueue::push(RadioPacketQueue::barrier b,
                            std::unique_ptr<RadioPacket> pkt)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace(b, std::move(pkt));
    }

    cond_.notify_one();
}

RadioPacketQueue::barrier RadioPacketQueue::pushBarrier(void)
{
    auto pkt = std::make_unique<RadioPacket>();

    pkt->barrier = true;

    RadioPacketQueue::barrier b;

    {
        std::lock_guard<std::mutex> lock(m_);

        b = q_.emplace(q_.end(), std::move(pkt));
    }

    cond_.notify_one();

    return b;
}

void RadioPacketQueue::eraseBarrier(RadioPacketQueue::barrier b)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.erase(b);
    }

    cond_.notify_all();
}

bool RadioPacketQueue::pop(std::unique_ptr<RadioPacket>& pkt)
{
    std::unique_lock<std::mutex> lock(m_);

    cond_.wait(lock, [this]{ return done_ || (!q_.empty() && !q_.front()->barrier); });
    if (done_)
        return false;
    else {
        pkt = std::move(q_.front());
        q_.pop_front();
        return true;
    }
}

void RadioPacketQueue::stop(void)
{
    done_ = true;
    cond_.notify_all();
}
