// The network queue's send-window gate (P2 finding). A closed send window must
// hold back sequenced data to that node but never the controller's unsequenced
// ACK/control packets; otherwise a simultaneous open stalls until the SYN
// retransmission timer fires and the retransmitted SYN is delivered twice.
#include <cstdio>

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

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
