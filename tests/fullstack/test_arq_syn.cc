// SmartController receive-side SYN handling (P2 finding). A SYN whose ACK was
// lost is retransmitted with the same sequence number and data; the receiver
// must ACK it again without resetting its window and delivering the packet a
// second time. A SYN with different data still starts a new connection (the
// sender restarted).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Neighborhood.hh"
#include "llc/SmartController.hh"
#include "phy/NativeOFDM.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// Collects what the controller delivers toward the network.
struct Sink : public Element {
    Sink() : in(*this, nullptr, nullptr, [this](std::shared_ptr<RadioPacket> &&p) {
        const unsigned char *d = p->data() + sizeof(ExtendedHeader);
        got.emplace_back(reinterpret_cast<const char *>(d), p->ehdr().data_len);
    }) {}
    RadioIn<Push> in;
    std::vector<std::string> got;
};

static std::shared_ptr<RadioPacket> frame(uint16_t seq, bool syn, const std::string &data, uint16_t ack)
{
    std::vector<unsigned char> buf(sizeof(ExtendedHeader) + data.size());
    std::memcpy(buf.data() + sizeof(ExtendedHeader), data.data(), data.size());
    Header h = {};
    h.curhop = 1;
    h.nexthop = 2;
    h.seq = Seq{seq};
    h.flags.syn = syn;
    h.flags.has_seq = 1;
    h.flags.ack = ack != 0;
    auto p = std::make_shared<RadioPacket>(h, buf.data(), buf.size());
    p->ehdr().src = 1;
    p->ehdr().dest = 2;
    p->ehdr().ack = Seq{ack};
    p->ehdr().data_len = data.size();
    p->timestamp = MonoClock::now();
    p->evm = -30;
    p->rssi = -40;
    return p;
}

int main()
{
    PHY::setTeam(0);
    PHY::setNodeId(2);
    const liquid::MCS qpsk(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK);
    auto phy = std::make_shared<NativeOFDM>(std::vector<std::pair<liquid::MCS, AutoGain>>{{qpsk, AutoGain()}}, 48, 6, std::nullopt);
    auto nhood = std::make_shared<Neighborhood>(2);
    auto controller = std::make_shared<SmartController>(nhood, 1500, phy, 64, 64,
        std::vector<evm_thresh_t>{std::nullopt});
    auto sink = std::make_shared<Sink>();
    controller->radio_out.connect(sink, &sink->in);

    auto rx = [&](uint16_t seq, bool syn, const std::string &data, uint16_t ack = 0) {
        controller->radio_in.send(frame(seq, syn, data, ack));
    };

    rx(0, true, "first packet A");
    rx(0, true, "first packet A", 5);     // SYN retransmitted after its ACK was lost
    rx(0, true, "first packet A", 6);     // and again
    rx(1, false, "B");
    rx(2, false, "C");
    rx(0, true, "first packet A", 7);     // late duplicate SYN after the window advanced
    rx(3, false, "D");
    rx(1, false, "B");                    // ordinary duplicate
    rx(0, true, "restarted sender E");    // new connection: different data
    rx(1, false, "F");

    const std::vector<std::string> want = {"first packet A", "B", "C", "D", "restarted sender E", "F"};
    CHECK(sink->got == want, "delivered %zu packets, expected %zu", sink->got.size(), want.size());
    for (size_t i = 0; i < sink->got.size(); ++i)
        std::printf("  delivered %s\n", sink->got[i].c_str());

    controller->radio_out.disconnect();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
