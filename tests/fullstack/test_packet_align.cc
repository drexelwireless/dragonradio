// Control-message sequence numbers must be accessed at an alignment their
// type allows (P4 sanitizer finding).
//
// ControlMsg is a packed wire structure: the type byte is followed by a union,
// so every Seq inside it (selective ACK begin/end, NAK, set-unack) sits at an
// odd offset. Packet::iterator copies each message into a ControlMsg member,
// and SmartController compares those fields with Seq's member operators.
// With alignof(Seq) == 2 that is a member call on a misaligned object:
// undefined behavior, reported by UBSan in the erasure-arq scenario
// (SmartController::handleSelectiveACK). Seq now has alignment 1 (packed), so
// any address is valid; layouts of the packed wire structures are unchanged.
#include <cstdint>
#include <cstdio>

#include "Packet.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

template <class T>
static bool aligned(const T *p)
{
    return reinterpret_cast<uintptr_t>(p) % alignof(T) == 0;
}

int main()
{
    static_assert(sizeof(Seq) == 2, "Seq is 16 bits on the wire");
    static_assert(sizeof(Header) == 5, "Header layout unchanged");

    NetPacket pkt(sizeof(ExtendedHeader));
    pkt.hdr.flags = {0};
    pkt.ehdr().src = 1;
    pkt.ehdr().dest = 2;
    pkt.ehdr().ack = Seq{0};
    pkt.ehdr().data_len = 0;   // control messages follow the (empty) data
    pkt.appendSelectiveAck(Seq{10}, Seq{20});
    pkt.appendNak(Seq{7});
    pkt.appendSetUnack(Seq{65530});
    pkt.appendSelectiveAck(Seq{65534}, Seq{3});   // wraps

    unsigned n = 0;
    for (auto it = pkt.begin(); it != pkt.end(); ++it, ++n) {
        switch (it->type) {
            case ControlMsg::Type::kSelectiveAck:
                CHECK(aligned(&it->ack.begin) && aligned(&it->ack.end), "selective ACK fields misaligned for Seq");
                CHECK(it->ack.begin < it->ack.end, "selective ACK %u < %u", (unsigned) it->ack.begin, (unsigned) it->ack.end);
                break;
            case ControlMsg::Type::kNak:
                CHECK(aligned(&it->nak), "NAK misaligned for Seq");
                CHECK(it->nak == Seq{7}, "NAK value %u", (unsigned) it->nak);
                break;
            case ControlMsg::Type::kSetUnack:
                CHECK(aligned(&it->unack.unack), "set-unack misaligned for Seq");
                CHECK(it->unack.unack > Seq{65000}, "set-unack value %u", (unsigned) it->unack.unack);
                break;
            default:
                CHECK(false, "unexpected control message %u", (unsigned) it->type);
        }
    }
    CHECK(n == 4, "iterated %u control messages", n);
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
