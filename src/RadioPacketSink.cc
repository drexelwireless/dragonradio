#include "RadioPacketSink.hh"

RadioPacketSink::RadioPacketSink(std::shared_ptr<NET> net) :
    _net(net),
    _done(false)
{
    _worker_thread = std::thread(&RadioPacketSink::worker, this);
}

RadioPacketSink::~RadioPacketSink()
{
}

void RadioPacketSink::stop(void)
{
    _done = true;

    _q.stop();

    if (_worker_thread.joinable())
        _worker_thread.join();
}

bool RadioPacketSink::wantPacket(NodeId dest)
{
    return dest == _net->getNodeId();
}

void RadioPacketSink::push(std::unique_ptr<RadioPacket> pkt)
{
    _q.push(std::move(pkt));
}

void RadioPacketSink::worker(void)
{
    std::unique_ptr<RadioPacket> pkt;

    while (!_done) {
        _q.pop(pkt);
        if (_done)
            break;

        _net->sendPacket(std::move(pkt));
    }
}
