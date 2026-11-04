// The FD channelizer's first FFT must not read uninitialized memory.
//
// FDChannelizer::fftWorker performs overlap-save into an FFTW buffer whose
// first O (= P - 1) input samples hold the previous block's tail. They are
// zeroed only on a discontinuity, detected as iqbuf->seq != seq + 1 with seq
// starting at 0, but the MAC numbers its first IQ buffer 1 (MAC::rxWorker),
// so the first block read whatever the buffer's memory held. An FFT mixes
// every input sample into every bin, so one non-finite or huge garbage value
// makes the whole first block non-finite. The native PHY then throws ("stream
// must contain finite IQ") in a demodulation thread, which ends the radio.
//
// The test makes that garbage deterministic: glibc's M_PERTURB fills every
// allocation with 0x7f bytes (about 3.4e38 per float), and a high
// M_MMAP_THRESHOLD keeps the FFT buffers on the heap, as a radio's are after
// FFTW planning has raised glibc's dynamic threshold. It then feeds finite
// samples, numbered as the MAC numbers them, and checks that the demodulator
// sees only finite samples.
#include <malloc.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <pybind11/embed.h>

#include "Packet.hh"
#include "phy/FDChannelizer.hh"
#include "phy/NativeOFDM.hh"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// NativeOFDM whose demodulators only count the samples they are given.
class ProbePHY : public NativeOFDM {
public:
    using NativeOFDM::NativeOFDM;

    std::atomic<uint64_t> samples{0};
    std::atomic<uint64_t> nonfinite{0};

    class Probe : public PHY::PacketDemodulator {
    public:
        Probe(ProbePHY &p, unsigned chanidx, const Channel &channel)
          : PHY::PacketDemodulator(p, chanidx, channel)
          , probe_(p)
        {
        }

        bool isFrameOpen(void) override { return false; }

        void reset(const Channel &channel) override {}

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset,
                       size_t delay,
                       float rate,
                       float rx_rate) override
        {
        }

        void demodulate(const std::complex<float>* data, size_t count) override
        {
            uint64_t bad = 0;

            for (size_t i = 0; i < count; ++i)
                bad += !std::isfinite(data[i].real()) || !std::isfinite(data[i].imag());
            probe_.nonfinite += bad;
            probe_.samples += count;
        }

    private:
        ProbePHY &probe_;
    };

    std::shared_ptr<PHY::PacketDemodulator> mkPacketDemodulator(unsigned chanidx, const Channel &channel) override
    {
        return std::make_shared<Probe>(*this, chanidx, channel);
    }
};

int main()
{
    // Every allocation filled with 0x7f bytes; nothing mmapped (so nothing
    // comes back zeroed by the kernel).
    if (!getenv("FD_TEST_NO_PERTURB")) {
        mallopt(M_MMAP_THRESHOLD, 1 << 30);
        mallopt(M_PERTURB, 0x80);
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    pybind11::scoped_interpreter python;
    PHY::setTeam(0);
    PHY::setNodeId(1);

    const liquid::MCS qpsk(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK);
    auto phy = std::make_shared<ProbePHY>(std::vector<std::pair<liquid::MCS, AutoGain>>{{qpsk, AutoGain()}},
                                          48, 6, std::nullopt, 4096, 0, true, "v29");
    const double fs = 1e6;
    PHYChannel   chan(Channel(0.0, fs), phy, {std::nullopt});

    chan.taps = {std::complex<float>(1.0f, 0.0f)};

    // As the radio does: construct with no channels, then set them.
    auto channelizer = std::make_shared<FDChannelizer>(std::vector<PHYChannel>{}, fs, 1);

    channelizer->setChannels(std::vector<PHYChannel>{chan});

    // Four RX periods of small finite noise, numbered from 1 as MAC::rxWorker
    // numbers them after starting the stream.
    const size_t n = 100000;
    uint32_t     x = 1;

    for (unsigned seq = 1; seq <= 4; ++seq) {
        auto buf = std::make_shared<IQBuf>(n);

        for (size_t i = 0; i < n; ++i) {
            x = x*1664525u + 1013904223u;
            (*buf)[i] = std::complex<float>(1e-3f*static_cast<float>(x >> 16)/65536.0f, 0.0f);
        }
        buf->seq = seq;
        buf->fc = 0.0f;
        buf->fs = fs;
        buf->timestamp = MonoClock::now();
        channelizer->push(buf);
        buf->nsamples.store(n, std::memory_order_release);
        buf->complete.store(true, std::memory_order_release);
    }

    for (int i = 0; i < 200 && phy->samples.load() < 3*n; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::printf("demodulator saw %llu samples, %llu non-finite\n",
                (unsigned long long) phy->samples.load(), (unsigned long long) phy->nonfinite.load());
    CHECK(phy->samples.load() >= 3*n, "channelizer delivered %llu samples, expected at least %zu",
          (unsigned long long) phy->samples.load(), 3*n);
    CHECK(phy->nonfinite.load() == 0, "channelizer delivered %llu non-finite samples from finite input",
          (unsigned long long) phy->nonfinite.load());

    channelizer->stop();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
