// Downlink OFDMA burst batching in the channel synthesizer, and Queue::kick.
//
// Ported behavior: codex/ofdma-simulation batched OFDMA bursts in
// UnichannelSynthesizer (3b58785e); this branch's ChannelSynthesizer replaced
// it. For a PHY with getMaxBurstUsers() > 1 (NativeOFDMA), after the first
// unicast packet the worker takes a held packet for another next hop, or waits
// at most burst_wait for one; a kick of the network queue bounds that wait.
// Unsuitable packets are held in order. PHYs with one user per burst never
// wait (test_synth_slot covers them).
//
//  1. Queue::kick: a pop blocked on an empty queue returns false once; a pop
//     on a non-empty queue still returns its packet.
//  2. Packets alternating between two next hops: every burst carries two
//     packets for different next hops, in queue order.
//  3. One next hop only: single-user bursts; each waits about burst_wait for a
//     partner (bounded by the kick, not by the next packet).
//  4. Broadcast packets never carry a partner.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <pybind11/embed.h>

#include "Packet.hh"
#include "llc/Controller.hh"
#include "mac/Schedule.hh"
#include "net/MandateQueue.hh"
#include "net/SimpleQueue.hh"
#include "net/TailDropQueue.hh"
#include "phy/ChannelSynthesizer.hh"
#include "phy/ChannelSynthesizer.inl"
#include "phy/NativeOFDMA.hh"
#include "phy/TDChannelModulator.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

using Synth = ChannelSynthesizer<TDChannelModulator>;
using PktQueue = Queue<std::shared_ptr<NetPacket>>;

static std::shared_ptr<NetPacket> mkPacket(NodeId nexthop, uint16_t seq, size_t len = 500)
{
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + len);

    pkt->hdr.curhop = 1;
    pkt->hdr.nexthop = nexthop;
    pkt->hdr.seq = Seq{seq};
    pkt->hdr.flags = {0};
    pkt->ehdr().src = 1;
    pkt->ehdr().dest = nexthop;
    pkt->ehdr().data_len = len;
    pkt->mcsidx = 0;
    return pkt;
}

// Kick semantics for one queue type.
static void testKick(const char *name, std::shared_ptr<PktQueue> q)
{
    q->enable();

    std::shared_ptr<NetPacket> pkt;
    std::atomic<bool>          returned{false};
    bool                       ok = true;
    std::thread                t([&]{ ok = q->pop(pkt); returned = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!returned, "%s: pop on an empty queue returned before a kick", name);
    q->kick();
    t.join();
    CHECK(returned && !ok, "%s: kicked pop on an empty queue should return false", name);

    q->push(mkPacket(2, 0));
    q->kick();
    ok = q->pop(pkt);
    CHECK(ok && pkt && pkt->hdr.nexthop == 2, "%s: pop after a kick on a non-empty queue should return the packet", name);
}

// Run the synthesizer over one slot holding the queued packets and return it.
static TXSlot runSlot(Synth &synth, double secs)
{
    synth.push_slot(WallClock::now() + WallClock::duration(10.0), 0, 0);
    std::this_thread::sleep_for(std::chrono::duration<double>(secs));
    return synth.pop_slot();
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    pybind11::scoped_interpreter python;
    PHY::setTeam(0);
    PHY::setNodeId(1);

    // 1. Queue::kick
    testKick("SimpleQueue", std::make_shared<SimpleQueue<std::shared_ptr<NetPacket>>>(SimpleQueue<std::shared_ptr<NetPacket>>::FIFO));
    testKick("TailDropQueue", std::make_shared<TailDropQueue<std::shared_ptr<NetPacket>>>(100000));
    testKick("MandateQueue", std::make_shared<MandateQueue<std::shared_ptr<NetPacket>>>());

    const liquid::MCS qpsk(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK);
    auto phy = std::make_shared<NativeOFDMA>(std::vector<std::pair<liquid::MCS, AutoGain>>{{qpsk, AutoGain()}},
                                             48, 6, std::nullopt, 4096, true, "equal");
    const double fs = 1e6;
    PHYChannel   chan(Channel(0.0, fs), phy, {std::nullopt});

    chan.taps = {std::complex<float>(1.0f, 0.0f)};

    auto synth = std::make_shared<Synth>(std::vector<PHYChannel>{chan}, fs, 1);

    // A long slot: everything queued fits.
    synth->setSchedule(Schedule(Schedule::sched_type{{true}}, std::chrono::duration<double>(1.0), std::chrono::duration<double>(0.0)));

    auto queue = std::make_shared<SimpleQueue<std::shared_ptr<NetPacket>>>(SimpleQueue<std::shared_ptr<NetPacket>>::FIFO);

    synth->sink.connect(queue, &queue->out);
    synth->setBurstQueue(queue);
    synth->setBurstWait(0.020);

    // 2. Alternating next hops 2, 3: five bursts of two.
    for (uint16_t i = 0; i < 10; ++i)
        queue->push(mkPacket(i % 2 ? 3 : 2, i));
    {
        TXSlot slot = runSlot(*synth, 0.3);
        auto  &mpkts = slot.txrecord.mpkts;
        unsigned shared = 0, order_ok = 1;
        uint16_t expect = 0;

        for (auto &m : mpkts) {
            if (m->partners.size() == 1 && m->partners[0]->hdr.nexthop != m->pkt->hdr.nexthop)
                shared++;
            order_ok &= static_cast<uint16_t>(m->pkt->hdr.seq) == expect;
            expect += 1 + m->partners.size();
        }
        std::printf("alternating next hops: %zu bursts, %u with two packets\n", mpkts.size(), shared);
        CHECK(mpkts.size() == 5 && shared == 5, "expected 5 two-packet bursts, got %zu bursts, %u shared", mpkts.size(), shared);
        CHECK(order_ok, "bursts out of queue order");
    }

    // 3. One next hop: single-user bursts, each after about one burst wait.
    for (uint16_t i = 0; i < 4; ++i)
        queue->push(mkPacket(2, 100 + i));
    {
        auto   t0 = std::chrono::steady_clock::now();
        TXSlot slot = runSlot(*synth, 0.4);
        auto  &mpkts = slot.txrecord.mpkts;
        size_t partners = 0;

        for (auto &m : mpkts)
            partners += m->partners.size();
        std::printf("one next hop: %zu bursts, %zu partners (%.0f ms)\n", mpkts.size(), partners,
                    1e3*std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        CHECK(mpkts.size() == 4 && partners == 0, "expected 4 single-user bursts, got %zu bursts and %zu partners", mpkts.size(), partners);
    }

    // The wait is bounded by the kick: with an empty queue a lone packet is
    // modulated about burst_wait after it is pulled, not when the next one
    // arrives.
    {
        synth->push_slot(WallClock::now() + WallClock::duration(10.0), 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        queue->push(mkPacket(2, 200));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        TXSlot slot = synth->pop_slot();

        CHECK(slot.txrecord.mpkts.size() == 1, "lone packet not modulated within 150 ms (burst wait 20 ms): %zu", slot.txrecord.mpkts.size());
    }

    // 4. Broadcast first: no partner; the unicast packets after it pair.
    queue->push(mkPacket(kNodeBroadcast, 300));
    queue->push(mkPacket(2, 301));
    queue->push(mkPacket(3, 302));
    {
        TXSlot slot = runSlot(*synth, 0.3);
        auto  &mpkts = slot.txrecord.mpkts;

        CHECK(mpkts.size() == 2, "expected a broadcast burst and a two-packet burst, got %zu bursts", mpkts.size());
        if (mpkts.size() == 2) {
            CHECK(mpkts.front()->pkt->hdr.nexthop == kNodeBroadcast && mpkts.front()->partners.empty(),
                  "broadcast burst carried a partner");
            CHECK(mpkts.back()->partners.size() == 1, "unicast packets after the broadcast did not pair");
        }
    }

    synth->stop();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
