// Native downlink OFDMA PHY for the production stack (P5). See NativeOFDMA.hh.
#include <cstring>
#include <mutex>
#include <stdexcept>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "Logger.hh"
#include "WorkQueue.hh"
#include "dsp/FFTW.hh"
#include "phy/NativeOFDMA.hh"

using namespace std::chrono_literals;
using dragonradio::native::FramedOFDMA;
using dragonradio::native::FramedOFDMAStream;
using dragonradio::native::OFDMASlotInput;
using dragonradio::native::OFDMAUser;
namespace nv = dragonradio::native;

NativeOFDMA::NativeOFDMA(const std::vector<std::pair<liquid::MCS, AutoGain>> &mcs_table,
                         unsigned int M,
                         unsigned int cp_len,
                         const std::optional<std::string> &subcarriers,
                         unsigned int max_payload,
                         bool soft_payload,
                         const std::string &allocation)
  : NativeOFDM(mcs_table, M, cp_len, subcarriers, max_payload, 0, soft_payload, "v29")
  , allocation_name_(allocation)
{
    if (!fecAvailable())
        throw std::invalid_argument("native-ofdma needs native FEC (its control header is v29 coded)");
    if (allocation == "equal")
        two_user_alloc_ = 0;
    else if (allocation == "1:2")
        two_user_alloc_ = 1;
    else if (allocation == "2:1")
        two_user_alloc_ = 2;
    else
        throw std::invalid_argument("native-ofdma allocation must be equal, 1:2 or 2:1");
    mkOFDMA();
}

std::unique_ptr<FramedOFDMA> NativeOFDMA::mkOFDMA(void) const
{
    std::lock_guard<std::mutex> lock(fftw::mutex);
    auto f = std::make_unique<FramedOFDMA>(static_cast<int>(M_), static_cast<int>(cp_len_), data_bins_, pilot_bins_,
                                           static_cast<int>(max_payload_), max_on_air_);
    f->base.soft = soft_payload_;
    return f;
}

size_t NativeOFDMA::getModulatedSize(mcsidx_t mcsidx, size_t n)
{
    if (mcsidx >= bps_.size())
        throw std::out_of_range("MCS index out of range");
    auto f = mkOFDMA();
    std::array<OFDMASlotInput, nv::kMaxUsers> in;
    unsigned char info[nv::kHeaderInfoBytes] = {0};
    in[0].info = info;
    in[0].bps = bps_[mcsidx];
    in[0].length = static_cast<int>(sizeof(ExtendedHeader) + n);
    in[0].fec0 = fec_[mcsidx].first;
    in[0].fec1 = fec_[mcsidx].second;
    size_t samples = f->frame_samples(3, in);
    std::lock_guard<std::mutex> lock(fftw::mutex);
    f.reset();
    return samples;
}

std::shared_ptr<PHY::PacketModulator> NativeOFDMA::mkPacketModulator(void)
{
    return std::make_shared<PacketModulator>(*this);
}

std::shared_ptr<PHY::PacketDemodulator> NativeOFDMA::mkPacketDemodulator(unsigned chanidx, const Channel &channel)
{
    return std::make_shared<PacketDemodulator>(*this, chanidx, channel);
}

// ---------------------------------------------------------------- modulator

NativeOFDMA::PacketModulator::PacketModulator(NativeOFDMA &phy)
  : PHY::PacketModulator(phy)
  , ofdma_(phy)
  , frame_(phy.mkOFDMA())
{
}

NativeOFDMA::PacketModulator::~PacketModulator()
{
    std::lock_guard<std::mutex> lock(fftw::mutex);
    frame_.reset();
}

void NativeOFDMA::PacketModulator::modulate(std::shared_ptr<NetPacket> pkt, const float g, ModPacket &mpkt)
{
    modulateBurst({std::move(pkt)}, g, mpkt);
}

void NativeOFDMA::PacketModulator::modulateBurst(std::vector<std::shared_ptr<NetPacket>> pkts, const float g, ModPacket &mpkt)
{
    MonoClock::time_point mod_start = MonoClock::now();

    if (pkts.empty() || pkts.size() > nv::kMaxUsers)
        throw std::invalid_argument("native-ofdma bursts carry one or two packets");
    if (pkts.size() == 2 && pkts[0]->hdr.nexthop == pkts[1]->hdr.nexthop)
        throw std::invalid_argument("native-ofdma burst users must have different next hops");
    const int alloc = pkts.size() == 1 ? 3 : ofdma_.two_user_alloc_;
    std::array<OFDMASlotInput, nv::kMaxUsers> in;
    for (size_t u = 0; u < pkts.size(); ++u) {
        NetPacket &pkt = *pkts[u];

        pkt.hdr.flags.team = team_;
        if (pkt.mcsidx >= ofdma_.bps_.size())
            throw std::out_of_range("MCS index out of range");
        if (pkt.size() > ofdma_.max_payload_)
            throw std::length_error("packet exceeds native-ofdma maximum payload");
        in[u].info = reinterpret_cast<const unsigned char*>(&pkt.hdr);
        in[u].mcsidx = static_cast<int>(pkt.mcsidx);
        in[u].bps = ofdma_.bps_[pkt.mcsidx];
        in[u].payload = pkt.data();
        in[u].length = static_cast<int>(pkt.size());
        in[u].fec0 = ofdma_.fec_[pkt.mcsidx].first;
        in[u].fec1 = ofdma_.fec_[pkt.mcsidx].second;
    }
    const int nsamples = frame_->frame_samples(alloc, in);
    auto iqbuf = std::make_shared<IQBuf>(nsamples);

    frame_->write(alloc, in, iqbuf->data(), nsamples);

    if (g != 1.0)
        xsimd::transform(iqbuf->data(), iqbuf->data() + nsamples, iqbuf->data(), [&](const auto& x) { return x*g; });

    MonoClock::time_point mod_end = MonoClock::now();

    // AutoGain of the first packet's MCS sees the whole burst.
    AutoGain &autogain = phy_.mcs_table[pkts[0]->mcsidx].autogain;

    if (autogain.needCalcAutoSoftGain0dBFS())
        work_queue.submit(&AutoGain::autoSoftGain0dBFS, &autogain, g, iqbuf);

    for (auto &p : pkts) {
        p->mod_start_timestamp = mod_start;
        p->mod_end_timestamp = mod_end;
    }
    mpkt.offset = iqbuf->delay;
    mpkt.nsamples = iqbuf->size() - iqbuf->delay;
    mpkt.samples = std::move(iqbuf);
    mpkt.pkt = std::move(pkts[0]);
    mpkt.partners.assign(std::make_move_iterator(pkts.begin() + 1), std::make_move_iterator(pkts.end()));
}

// -------------------------------------------------------------- demodulator

NativeOFDMA::PacketDemodulator::PacketDemodulator(NativeOFDMA &phy, unsigned chanidx, const Channel &channel)
  : PHY::PacketDemodulator(phy, chanidx, channel)
  , ofdma_(phy)
  , frame_(phy.mkOFDMA())
  , timestamp_(0.0s)
{
    // Demodulate only the user slots the production filter wants.
    stream_ = std::make_unique<FramedOFDMAStream>(*frame_, [](const OFDMAUser &u) {
        Header hdr;
        std::memcpy(&hdr, u.info, sizeof(Header));
        return PHY::wantPacket(true, &hdr);
    });
}

NativeOFDMA::PacketDemodulator::~PacketDemodulator()
{
    std::lock_guard<std::mutex> lock(fftw::mutex);
    stream_.reset();
    frame_.reset();
}

bool NativeOFDMA::PacketDemodulator::isFrameOpen(void)
{
    return stream_->holds_samples();
}

void NativeOFDMA::PacketDemodulator::reset(const Channel &channel)
{
    stream_->reset();
    channel_ = channel;
    resamp_rate_ = 1.0;
    timestamp_ = MonoClock::time_point{0.0s};
    snapshot_off_ = std::nullopt;
    offset_ = 0;
    delay_ = 0;
    sample_start_ = 0;
    sample_end_ = 0;
}

void NativeOFDMA::PacketDemodulator::timestamp(const MonoClock::time_point &timestamp, std::optional<ssize_t> snapshot_off,
                                               ssize_t offset, size_t delay, float rate, float rx_rate)
{
    resamp_rate_ = 1.0/rate;
    rx_rate_ = rx_rate;
    timestamp_ = timestamp;
    snapshot_off_ = snapshot_off;
    offset_ = offset;
    delay_ = delay;
    sample_start_ = sample_end_;
}

void NativeOFDMA::PacketDemodulator::demodulate(const std::complex<float>* data, size_t count)
{
    results_.clear();
    stream_->feed(data, static_cast<int>(count), false, results_);
    sample_end_ += static_cast<long long>(count);
    for (const auto &r : results_)
        deliver(r);
}

void NativeOFDMA::PacketDemodulator::deliver(const FramedOFDMAStream::Result &r)
{
    auto now = MonoClock::now();

    if (!r.header_valid) {
        if (log_invalid_headers_)
            logPHY(LOGDEBUG-1, "invalid OFDMA control header");
        return;
    }

    ssize_t start = offset_ - delay_ + static_cast<ssize_t>(resamp_rate_*static_cast<double>(r.start - sample_start_));
    ssize_t end = offset_ - delay_ + static_cast<ssize_t>(resamp_rate_*static_cast<double>(r.start + r.samples - sample_start_));

    // Slots for other next hops were not demodulated.
    if (!r.wanted)
        return;

    Header hdr;
    std::memcpy(&hdr, r.user.info, sizeof(Header));
    std::vector<unsigned char> payload(r.payload);
    std::shared_ptr<RadioPacket> pkt = PHY::mkRadioPacket(true, r.payload_valid && r.samples > 0, hdr, payload.size(),
                                                          payload.empty() ? nullptr : payload.data());
    if (!pkt)
        return;

    pkt->initMGENInfo();
    pkt->evm = static_cast<float>(r.evm);
    pkt->rssi = static_cast<float>(r.rssi_db);
    pkt->cfo = static_cast<float>(r.cfo / ofdma_.M_);
    pkt->chanidx = chanidx_;
    pkt->channel = channel_;
    pkt->bw = rx_rate_;
    pkt->mcsidx = r.user.mcsidx < static_cast<int>(ofdma_.bps_.size()) ? r.user.mcsidx : 0;
    MonoClock::time_point timestamp = timestamp_ + MonoClock::duration(start / rx_rate_);
    pkt->timestamp = timestamp;
    pkt->slot_timestamp = timestamp_;
    pkt->start_samples = start;
    pkt->end_samples = end;
    pkt->demod_latency = (now - timestamp).count();
    pkt->payload_len = payload.size();

    callback_(std::move(pkt));

    if (snapshot_off_)
        snapshot_collector_->selfTX(*snapshot_off_ + start, *snapshot_off_ + end, channel_.fc, channel_.bw);
}
