// The network queue's send-window gate (P2 finding). A closed send window must
// hold back sequenced data to that node but never the controller's unsequenced
// ACK/control packets; otherwise a simultaneous open stalls until the SYN
// retransmission timer fires and the retransmitted SYN is delivered twice.
#include <cmath>
#include <cstdio>
#include <vector>

#include "net/REDQueue.hh"
#include "net/TailDropQueue.hh"
#include "net/SimpleQueue.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static std::shared_ptr<NetPacket> packet(NodeId nexthop, bool has_seq, bool assigned)
{
    auto p = std::make_shared<NetPacket>(sizeof(ExtendedHeader));
    p->hdr.curhop = 1;
    p->hdr.nexthop = nexthop;
    p->hdr.flags = {0};
    p->hdr.flags.has_seq = has_seq;
    p->internal_flags.assigned_seq = assigned;
    p->ehdr().data_len = 0;
    return p;
}

int main()
{
    for (auto type : {SimpleNetQueue::FIFO, SimpleNetQueue::LIFO}) {
        SimpleNetQueue q(type);
        std::shared_ptr<NetPacket> out;

        q.setSendWindowStatus(2, false);
        q.push(packet(2, true, false));          // new data to node 2: held
        q.push_hi(packet(2, false, false));      // ACK-only to node 2: passes
        q.push(packet(3, true, false));          // data to node 3: passes

        CHECK(q.pop(out) && out->hdr.nexthop == 2 && out->hdr.flags.has_seq == 0, "ACK-only packet held by closed window");
        CHECK(q.pop(out) && out->hdr.nexthop == 3, "data to an open node");

        q.repush(packet(2, true, true));         // retransmission (assigned seq): passes
        CHECK(q.pop(out) && out->hdr.nexthop == 2 && out->internal_flags.assigned_seq, "retransmission held");

        q.setSendWindowStatus(2, true);
        CHECK(q.pop(out) && out->hdr.nexthop == 2 && out->hdr.flags.has_seq == 1, "data released when window opens");
    }

    // Tail drop with an unreachable destination (P5 finding): packets to a
    // node whose send window is closed cannot leave the queue. When the queue
    // is full they must not starve other destinations: a packet to an open
    // node pushes out the oldest packet to a closed one. With every window
    // open, or a single destination, tail drop is unchanged.
    // (pop() waits only while the queue is empty, then returns false if no
    // packet may leave, so every pop below returns.)
    {
        auto data = [](NodeId nexthop, uint16_t seq, unsigned bytes) {
            auto p = packet(nexthop, true, false);
            p->hdr.seq = Seq{seq};
            p->payload_size = bytes;
            return p;
        };
        TailDropQueue<std::shared_ptr<NetPacket>> q(3000);
        std::shared_ptr<NetPacket> out;

        q.setSendWindowStatus(3, false);
        for (uint16_t i = 0; i < 4; ++i)
            q.push(data(3, i, 1000));          // the queue fills with blocked packets (4000 bytes)
        q.push(data(2, 100, 1000));            // a packet to an open node arrives
        CHECK(q.pop(out) && out->hdr.nexthop == 2, "packet to an open node starved by a closed node's packets");
        q.setSendWindowStatus(3, true);
        std::vector<unsigned> kept;
        for (int i = 0; i < 3; ++i)
            if (q.pop(out) && out->hdr.nexthop == 3)
                kept.push_back(static_cast<uint16_t>(out->hdr.seq));
        CHECK(kept == std::vector<unsigned>({1, 2, 3}), "the oldest blocked packet is the one pushed out (kept %zu)", kept.size());

        // Unchanged when the arrival's own window is closed, or every window is open.
        TailDropQueue<std::shared_ptr<NetPacket>> q2(3000);
        q2.setSendWindowStatus(3, false);
        for (uint16_t i = 0; i < 4; ++i)
            q2.push(data(3, i, 1000));
        q2.push(data(3, 9, 1000));             // also blocked: dropped as before
        q2.setSendWindowStatus(3, true);
        std::vector<unsigned> seqs;
        for (int i = 0; i < 4; ++i)
            if (q2.pop(out))
                seqs.push_back(static_cast<uint16_t>(out->hdr.seq));
        CHECK(seqs == std::vector<unsigned>({0, 1, 2, 3}), "blocked arrival into a full queue not dropped");
        TailDropQueue<std::shared_ptr<NetPacket>> q3(3000);
        for (uint16_t i = 0; i < 4; ++i)
            q3.push(data(2, i, 1000));
        q3.push(data(4, 9, 1000));             // all open: tail drop drops the arrival
        seqs.clear();
        for (int i = 0; i < 4; ++i)
            if (q3.pop(out))
                seqs.push_back(static_cast<uint16_t>(out->hdr.seq));
        CHECK(seqs == std::vector<unsigned>({0, 1, 2, 3}), "open-window tail drop changed");

        // Push-out is the default; setPushOut(false) restores plain tail drop,
        // where the open node's packet is dropped on arrival.
        TailDropQueue<std::shared_ptr<NetPacket>> q4(3000);
        CHECK(q4.getPushOut(), "push-out is not the default");
        q4.setPushOut(false);
        q4.setSendWindowStatus(3, false);
        for (uint16_t i = 0; i < 4; ++i)
            q4.push(data(3, i, 1000));
        q4.push(data(2, 100, 1000));
        CHECK(!q4.pop(out), "plain tail drop kept the arrival at a full queue");
        q4.setSendWindowStatus(3, true);
        seqs.clear();
        for (int i = 0; i < 4; ++i)
            if (q4.pop(out))
                seqs.push_back(static_cast<uint16_t>(out->hdr.seq));
        CHECK(seqs == std::vector<unsigned>({0, 1, 2, 3}), "plain tail drop pushed out a blocked packet");
    }

    // Gentle RED: between max_thresh and 2*max_thresh the drop probability
    // rises linearly from max_p to 1. With w_q = 1 the average equals the
    // instantaneous queue size, which is held fixed while probes arrive.
    for (double fill : {2200.0, 3000.0, 3800.0}) {
        REDNetQueue q(true, 1000, 2000, 0.1, 1.0);
        auto big = packet(2, true, false);
        big->payload_size = static_cast<size_t>(fill);
        q.push(std::move(big));
        const int trials = 20000;
        int dropped = 0;
        for (int i = 0; i < trials; ++i) {
            auto probe = packet(2, true, false);
            probe->payload_size = 1;
            const size_t before = q.size_;
            q.push(std::move(probe));
            if (q.size_ == before)
                ++dropped;
            else {
                q.q_.pop_back();      // keep the queue size fixed
                q.size_ = before;
            }
        }
        const double want = 0.1 + 0.9 * (fill - 2000) / 2000;
        const double got = static_cast<double>(dropped) / trials;
        CHECK(std::abs(got - want) < 0.03, "RED gentle drop probability at %g bytes: %.3f, expected %.3f", fill, got, want);
        std::printf("  RED avg %g bytes: drop probability %.3f (expected %.3f)\n", fill, got, want);
    }

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
