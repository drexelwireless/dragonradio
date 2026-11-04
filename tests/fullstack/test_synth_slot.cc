// The channel synthesizer must not re-modulate a packet that does not fit the
// rest of a slot.
//
// Ported from codex/ofdma-simulation (f0f0f3b8, where the bug was in
// UnichannelSynthesizer) to this branch's ChannelSynthesizer, which replaced
// the Unichannel/Slot synthesizers and had the same bug: when push() failed
// because the modulated packet did not fit in the slot's remaining samples,
// modWorker kept the packet and went back to wait_until_can_push(), which
// only asks whether one more sample fits. While the slot stayed open, the
// worker modulated the same packet again in a tight loop. A packet that does
// not fit cannot fit later in the same slot: the high-water mark is fixed
// when the slot is pushed and other threads only add samples.
//
// The test opens one slot with room for a frame and a half and a backlog in
// the queue, holds it open for 200 ms, and counts modulator calls. It then
// checks that the packet that did not fit leads the next slot.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <pybind11/embed.h>

#include "Packet.hh"
#include "logging.hh"
#include "llc/Controller.hh"
#include "mac/Schedule.hh"
#include "net/SimpleQueue.hh"
#include "phy/ChannelSynthesizer.hh"
#include "phy/ChannelSynthesizer.inl"
#include "phy/NativeOFDM.hh"
#include "phy/TDChannelModulator.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// NativeOFDM whose modulators count calls.
class CountingPHY : public NativeOFDM {
public:
    using NativeOFDM::NativeOFDM;

    std::atomic<unsigned> count{0};

    class Mod : public PHY::PacketModulator {
    public:
        explicit Mod(CountingPHY &p) : PHY::PacketModulator(p), phy_(p), inner_(p.NativeOFDM::mkPacketModulator()) {}

        void modulate(std::shared_ptr<NetPacket> pkt, const float g, ModPacket &mpkt) override
        {
            phy_.count++;
            inner_->modulate(std::move(pkt), g, mpkt);
        }

    private:
        CountingPHY &phy_;
        std::shared_ptr<PHY::PacketModulator> inner_;
    };

    std::shared_ptr<PHY::PacketModulator> mkPacketModulator(void) override
    {
        return std::make_shared<Mod>(*this);
    }
};

using Synth = ChannelSynthesizer<TDChannelModulator>;

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Synthesizer::stop releases the GIL (pybind11), so it needs an interpreter.
    pybind11::scoped_interpreter python;
    PHY::setTeam(0);
    PHY::setNodeId(1);
    const liquid::MCS qpsk(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK);
    auto phy = std::make_shared<CountingPHY>(std::vector<std::pair<liquid::MCS, AutoGain>>{{qpsk, AutoGain()}},
                                             48, 6, std::nullopt, 4096, 0, true, "v29");
    const double fs = 1e6;
    const size_t frame = phy->getModulatedSize(0, 1000);

    PHYChannel chan(Channel(0.0, fs), phy, {std::nullopt});

    chan.taps = {std::complex<float>(1.0f, 0.0f)};

    auto synth = std::make_shared<Synth>(std::vector<PHYChannel>{chan}, fs, 1);
    // One slot per frame and a half of samples, no guard: the first packet
    // fits, the second does not.
    const std::chrono::duration<double> slot_size((frame + frame/2)/fs);

    synth->setSchedule(Schedule(Schedule::sched_type{{true}}, slot_size, std::chrono::duration<double>(0.0)));

    auto queue = std::make_shared<SimpleQueue<std::shared_ptr<NetPacket>>>(SimpleQueue<std::shared_ptr<NetPacket>>::FIFO);

    synth->sink.connect(queue, &queue->out);

    for (unsigned i = 0; i < 20; ++i) {
        auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + 1000);
        pkt->hdr.curhop = 1;
        pkt->hdr.nexthop = 2;
        pkt->hdr.seq = Seq{static_cast<uint16_t>(i)};
        pkt->hdr.flags = {0};
        pkt->ehdr().src = 1;
        pkt->ehdr().dest = 2;
        pkt->ehdr().data_len = 1000;
        pkt->mcsidx = 0;
        queue->push(std::move(pkt));
    }

    auto now = WallClock::now();

    synth->push_slot(now + WallClock::duration(1.0), 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const unsigned during = phy->count.load();
    TXSlot         slot1 = synth->pop_slot();
    const size_t   in_slot = slot1.txrecord.mpkts.size();

    std::printf("slot held open 200 ms: %u modulations, %zu packet(s) in the slot (frame %zu samples)\n",
                during, in_slot, frame);
    CHECK(in_slot == 1, "expected one packet in the slot, got %zu", in_slot);
    // One packet fits and one is tried; anything above that is re-modulation.
    CHECK(during <= 2, "packet that did not fit was modulated %u times in one slot (expected at most 2)", during);

    // Next slot: the carried packet (seq 1) goes first.
    synth->push_slot(now + WallClock::duration(2.0), 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TXSlot slot2 = synth->pop_slot();

    CHECK(slot2.txrecord.mpkts.size() == 1, "expected one packet in the second slot, got %zu", slot2.txrecord.mpkts.size());
    if (!slot2.txrecord.mpkts.empty())
        CHECK(static_cast<uint16_t>(slot2.txrecord.mpkts.front()->pkt->hdr.seq) == 1,
              "second slot starts with seq %u, expected the carried packet 1",
              static_cast<unsigned>(static_cast<uint16_t>(slot2.txrecord.mpkts.front()->pkt->hdr.seq)));
    std::printf("total modulations after two slots: %u\n", phy->count.load());

    synth->stop();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
