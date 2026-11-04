// Slot synthesizer must not re-modulate a packet that does not fit (P4).
//
// UnichannelSynthesizer (the TDMA timedomain and freqdomain slot
// synthesizers) modulates queued packets into the current slot. A packet
// that does not fit in the slot's remaining samples cannot fit later in the
// same slot: max_samples is fixed when the slot starts and other modulator
// threads only add samples. Before the fix the worker kept the packet and,
// while the slot stayed open, modulated it again in a tight loop, so a node
// under saturating traffic burnt a whole core re-modulating one packet
// thousands of times per slot (seen in the P4 soak as dr-mod0 100% busy).
// MultichannelSynthesizer already stopped at the first packet that does not
// fit and carried it to the next slot; UnichannelSynthesizer now does too.
//
// The test holds one slot open for 200 ms with room for one frame and a
// backlog in the queue, then counts modulator calls. It also checks that the
// packet that did not fit is the first one modulated into the next slot.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include <pybind11/embed.h>

#include "Packet.hh"
#include "net/SimpleQueue.hh"
#include "phy/NativeOFDM.hh"
#include "phy/TDChannelModulator.hh"
#include "phy/UnichannelSynthesizer.hh"
#include "phy/UnichannelSynthesizer.inl"

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

protected:
    std::shared_ptr<PHY::PacketModulator> mkPacketModulator(void) override
    {
        return std::make_shared<Mod>(*this);
    }
};

using Synth = UnichannelSynthesizer<TDChannelModulator>;
using Slot = SlotSynthesizer::Slot;

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
    Channels channels{{Channel(0.0, fs), {C(1.0f, 0.0f)}}};
    auto synth = std::make_shared<Synth>(phy, fs, channels, 1);
    synth->setSchedule(Schedule::sched_type{{true}});
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

    // A slot with room for one frame and a half: the first packet fits, the
    // second does not.
    auto now = WallClock::now();
    auto slot = std::make_shared<Slot>(now + 1.0, 0, frame + frame/2, 2*frame, 0, 1);
    synth->modulate(slot);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->closed.store(true, std::memory_order_relaxed);
    }
    const unsigned during = phy->count.load();
    size_t in_slot;
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        in_slot = slot->mpkts.size();
    }
    std::printf("slot held open 200 ms: %u modulations, %zu packet(s) in the slot (frame %zu samples)\n",
                during, in_slot, frame);
    CHECK(in_slot == 1, "expected one packet in the slot, got %zu", in_slot);
    // One packet fits and one is tried; anything above that is re-modulation.
    CHECK(during <= 2, "packet that did not fit was modulated %u times in one slot (expected at most 2)", during);

    // Next slot: the carried packet (seq 1) goes first.
    auto slot2 = std::make_shared<Slot>(now + 2.0, 0, frame + frame/2, 2*frame, 0, 1);
    synth->modulate(slot2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(slot2->mutex);
        slot2->closed.store(true, std::memory_order_relaxed);
        CHECK(slot2->mpkts.size() == 1, "expected one packet in the second slot, got %zu", slot2->mpkts.size());
        if (!slot2->mpkts.empty())
            CHECK(static_cast<uint16_t>(slot2->mpkts.front()->pkt->hdr.seq) == 1,
                  "second slot starts with seq %u, expected the carried packet 1",
                  static_cast<unsigned>(static_cast<uint16_t>(slot2->mpkts.front()->pkt->hdr.seq)));
    }
    std::printf("total modulations after two slots: %u\n", phy->count.load());

    synth->stop();
    queue->stop();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
