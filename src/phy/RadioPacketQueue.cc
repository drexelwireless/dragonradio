// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "phy/RadioPacketQueue.hh"

RadioPacketQueue::RadioPacketQueue() :
    done_(false)
{
}

RadioPacketQueue::~RadioPacketQueue()
{
    stop();
}

void RadioPacketQueue::push(const std::shared_ptr<RadioPacket> &pkt)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace_back(pkt);
    }

    cond_.notify_one();
}

void RadioPacketQueue::push(barrier b,
                            const std::shared_ptr<RadioPacket> &pkt)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.insert(b, pkt);
    }

    cond_.notify_one();
}

RadioPacketQueue::barrier RadioPacketQueue::pushBarrier(void)
{
    RadioPacketQueue::barrier b;

    {
        std::lock_guard<std::mutex> lock(m_);

        b = q_.insert(q_.end(), std::monostate());
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

bool RadioPacketQueue::pop(std::shared_ptr<RadioPacket>& pkt)
{
    std::unique_lock<std::mutex> lock(m_);

    cond_.wait(lock, [this]{ return done_ || (!q_.empty() && std::holds_alternative<std::shared_ptr<RadioPacket>>(q_.front())); });
    if (done_)
        return false;
    else {
        pkt = std::move(std::get<std::shared_ptr<RadioPacket>>(q_.front()));
        q_.pop_front();
        return true;
    }
}

void RadioPacketQueue::stop(void)
{
    done_ = true;
    cond_.notify_all();
}
