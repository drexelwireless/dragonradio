// The network queue's send-window gate (P2 finding). A closed send window must
// hold back sequenced data to that node but never the controller's unsequenced
// ACK/control packets; otherwise a simultaneous open stalls until the SYN
// retransmission timer fires and the retransmitted SYN is delivered twice.
#include <cmath>
#include <cstdio>

#include "net/REDQueue.hh"
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
