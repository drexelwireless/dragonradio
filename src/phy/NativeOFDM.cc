// Native (non-liquidDSP) OFDM PHY for the production stack. See NativeOFDM.hh.
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "Logger.hh"
#include "WorkQueue.hh"
#include "dsp/FFTW.hh"
#include "liquid/OFDM.hh"
#include "phy/NativeOFDM.hh"

using dragonradio::native::FrameHeader;
using dragonradio::native::FramedPHY;
using dragonradio::native::FramedStream;

static_assert(sizeof(Header) == dragonradio::native::kHeaderInfoBytes,
              "native frame header carries exactly one DragonRadio Header");

NativeOFDM::NativeOFDM(const std::vector<std::pair<liquid::MCS, AutoGain>> &mcs_table,
                       unsigned int M,
                       unsigned int cp_len,
                       const std::optional<std::string> &subcarriers,
                       unsigned int max_payload,
                       unsigned int chase_bits)
  : M_(M)
  , cp_len_(cp_len)
  , max_payload_(max_payload)
  , chase_bits_(chase_bits)
{
    if (mcs_table.empty())
        throw std::invalid_argument("native-ofdm needs at least one MCS");
    if (max_payload > 65535)
        throw std::invalid_argument("native-ofdm maximum payload must be <= 65535 bytes");

    // Subcarrier layout: the same mask syntax and default as liquid OFDM.
    liquid::OFDMSubcarriers p = subcarriers ? liquid::OFDMSubcarriers(*subcarriers) : liquid::OFDMSubcarriers(M);
    if (p.size() != M)
        throw std::invalid_argument("subcarrier allocation must have M entries");
    for (unsigned k = 0; k < M; ++k) {
        if (p[k] == OFDMFRAME_SCTYPE_DATA)
            data_bins_.push_back(static_cast<int>(k));
        else if (p[k] == OFDMFRAME_SCTYPE_PILOT)
            pilot_bins_.push_back(static_cast<int>(k));
    }
    mask_ = static_cast<std::string>(p);

    // Capability check: no silent substitution of unsupported coding.
    mcs_.reserve(mcs_table.size());
    for (unsigned i = 0; i < mcs_table.size(); ++i) {
        const liquid::MCS &mcs = mcs_table[i].first;
        int bps = mcs.ms == LIQUID_MODEM_QPSK ? 2 : mcs.ms == LIQUID_MODEM_QAM16 ? 4 : 0;
        if (mcs.check != LIQUID_CRC_32 || mcs.fec0 != LIQUID_FEC_NONE || mcs.fec1 != LIQUID_FEC_NONE || bps == 0) {
            std::stringstream msg;
            msg << "native-ofdm does not support MCS " << i << " (" << mcs.description()
                << "); supported: crc32, no FEC, QPSK or 16-QAM";
            throw std::invalid_argument(msg.str());
        }
        mcs_.push_back(mcs);
        bps_.push_back(bps);
    }
    PHY::mcs_table.resize(mcs_.size());
    for (unsigned i = 0; i < mcs_.size(); ++i)
        PHY::mcs_table[i] = { &mcs_[i], mcs_table[i].second, true };

    // Validate the layout now rather than in a worker thread.
    mkFrame();
}

std::unique_ptr<FramedPHY> NativeOFDM::mkFrame(void) const
{
    // FFTW's planner is not thread-safe; production code serializes it with
    // fftw::mutex, and the engine additionally takes its own planner lock.
    std::lock_guard<std::mutex> lock(fftw::mutex);

    return std::make_unique<FramedPHY>(static_cast<int>(M_), static_cast<int>(cp_len_),
                                       data_bins_, pilot_bins_,
                                       static_cast<int>(max_payload_), static_cast<int>(chase_bits_));
}

size_t NativeOFDM::getModulatedSize(mcsidx_t mcsidx, size_t n)
{
    if (mcsidx >= bps_.size())
        throw std::out_of_range("MCS index out of range");

    std::unique_ptr<FramedPHY> frame = mkFrame();
    size_t samples = frame->frame_samples(static_cast<int>(sizeof(ExtendedHeader) + n), bps_[mcsidx]);

    std::lock_guard<std::mutex> lock(fftw::mutex);
    frame.reset();
    return samples;
}

std::shared_ptr<PHY::PacketModulator> NativeOFDM::mkPacketModulator(void)
{
    return std::make_shared<PacketModulator>(*this);
}

std::shared_ptr<PHY::PacketDemodulator> NativeOFDM::mkPacketDemodulator(void)
{
    return std::make_shared<PacketDemodulator>(*this);
}

// ---------------------------------------------------------------- modulator

NativeOFDM::PacketModulator::PacketModulator(NativeOFDM &phy)
  : PHY::PacketModulator(phy)
  , native_(phy)
  , frame_(phy.mkFrame())
{
}

NativeOFDM::PacketModulator::~PacketModulator()
{
    std::lock_guard<std::mutex> lock(fftw::mutex);
    frame_.reset();
}

void NativeOFDM::PacketModulator::modulate(std::shared_ptr<NetPacket> pkt,
                                           const float g,
                                           ModPacket &mpkt)
{
    MonoClock::time_point mod_start = MonoClock::now();

    // Set team in header
    pkt->hdr.flags.team = team_;

    if (pkt->mcsidx >= native_.bps_.size())
        throw std::out_of_range("MCS index out of range");
    if (pkt->size() > native_.max_payload_)
        throw std::length_error("packet exceeds native-ofdm maximum payload");

    const int bps = native_.bps_[pkt->mcsidx];
    const int nsamples = frame_->frame_samples(static_cast<int>(pkt->size()), bps);
    auto iqbuf = std::make_shared<IQBuf>(nsamples);

    frame_->write(reinterpret_cast<const unsigned char*>(&pkt->hdr),
                  static_cast<int>(pkt->mcsidx),
                  bps,
                  pkt->data(),
                  static_cast<int>(pkt->size()),
                  iqbuf->data(),
                  nsamples);

    // Apply soft gain.
    if (g != 1.0)
        xsimd::transform(iqbuf->data(),
                         iqbuf->data() + nsamples,
                         iqbuf->data(),
                         [&](const auto& x) { return x*g; });

    MonoClock::time_point mod_end = MonoClock::now();

    // Pass the modulated packet to the 0dBFS estimator if requested
    AutoGain &autogain = phy_.mcs_table[pkt->mcsidx].autogain;

    if (autogain.needCalcAutoSoftGain0dBFS())
        work_queue.submit(&AutoGain::autoSoftGain0dBFS, &autogain, g, iqbuf);

    pkt->mod_start_timestamp = mod_start;
    pkt->mod_end_timestamp = mod_end;

    mpkt.offset = iqbuf->delay;
    mpkt.nsamples = iqbuf->size() - iqbuf->delay;
    mpkt.samples = std::move(iqbuf);
    mpkt.pkt = std::move(pkt);
}

// -------------------------------------------------------------- demodulator

NativeOFDM::PacketDemodulator::PacketDemodulator(NativeOFDM &phy)
  : PHY::PacketDemodulator(phy)
  , native_(phy)
  , frame_(phy.mkFrame())
  , channel_()
  , timestamp_(0.0)
  , logger_(logger)
{
    // Ask the production packet filter (team, next hop, snapshot state)
    // before spending time on the payload; unwanted frames are skipped by
    // their announced length.
    stream_ = std::make_unique<FramedStream>(*frame_, [](const FrameHeader &h) {
        Header hdr;
        std::memcpy(&hdr, h.info, sizeof(Header));
        return PHY::wantPacket(true, &hdr);
    });
}

NativeOFDM::PacketDemodulator::~PacketDemodulator()
{
    std::lock_guard<std::mutex> lock(fftw::mutex);
    stream_.reset();
    frame_.reset();
}

bool NativeOFDM::PacketDemodulator::isFrameOpen(void)
{
    // TDChannelizer and FDChannelizer reset the demodulator at each new
    // receive buffer unless this returns true. Detection scans with lookahead,
    // so the tail of every buffer is unscanned when the next one arrives; a
    // preamble there would be lost by the reset. Report the stream as busy
    // while it holds such samples (P2 finding; contiguous buffers only, the
    // channelizers still reset on a sequence discontinuity).
    return stream_->holds_samples();
}

void NativeOFDM::PacketDemodulator::reset(const Channel &channel)
{
    stream_->reset();
    channel_ = channel;
    resamp_rate_ = 1.0;
    timestamp_ = MonoClock::time_point { 0.0 };
    snapshot_off_ = std::nullopt;
    offset_ = 0;
    delay_ = 0;
    sample_start_ = 0;
    sample_end_ = 0;
}

void NativeOFDM::PacketDemodulator::timestamp(const MonoClock::time_point &timestamp,
                                              std::optional<ssize_t> snapshot_off,
                                              ssize_t offset,
                                              size_t delay,
                                              float rate,
                                              float rx_rate)
{
    resamp_rate_ = 1.0/rate;
    rx_rate_ = rx_rate;
    timestamp_ = timestamp;
    snapshot_off_ = snapshot_off;
    offset_ = offset;
    delay_ = delay;
    sample_start_ = sample_end_;
}

void NativeOFDM::PacketDemodulator::demodulate(const std::complex<float>* data,
                                               size_t count)
{
    results_.clear();
    stream_->feed(data, static_cast<int>(count), false, results_);
    sample_end_ += static_cast<long long>(count);

    for (const auto &r : results_)
        deliver(r);
}

void NativeOFDM::PacketDemodulator::deliver(const FramedStream::Result &r)
{
    auto now = MonoClock::now();

    // Invalid headers produce no packet (mkRadioPacket logs them if asked);
    // frames for other nodes/teams were skipped without decoding a payload.
    if (!r.header_valid) {
        if (log_invalid_headers_)
            logPHY(LOGDEBUG-1, "invalid header");
        return;
    }
    if (!r.wanted)
        return;

    Header hdr;
    std::memcpy(&hdr, r.header.info, sizeof(Header));

    std::vector<unsigned char> payload(r.payload);
    std::shared_ptr<RadioPacket> pkt = PHY::mkRadioPacket(true,
                                                          r.payload_valid && r.samples > 0,
                                                          hdr,
                                                          payload.size(),
                                                          payload.empty() ? nullptr : payload.data());
    if (!pkt)
        return;

    pkt->initMGENInfo();

    // CSI: EVM [dB], RSSI [dB], CFO as f/Fs like the liquid PHYs.
    pkt->evm = static_cast<float>(r.evm);
    pkt->rssi = static_cast<float>(r.rssi_db);
    pkt->cfo = static_cast<float>(r.cfo / native_.M_);

    pkt->channel = channel_;
    pkt->bw = rx_rate_;
    pkt->mcsidx = r.header.mcsidx < static_cast<int>(native_.bps_.size()) ? r.header.mcsidx : 0;

    // Full-rate sample offsets of frame start/end relative to the slot.
    const long long frame_start = r.start;
    const long long frame_end = r.start + r.samples;
    ssize_t               start = offset_ - delay_ + static_cast<ssize_t>(resamp_rate_*static_cast<double>(frame_start - sample_start_));
    ssize_t               end = offset_ - delay_ + static_cast<ssize_t>(resamp_rate_*static_cast<double>(frame_end - sample_start_));
    MonoClock::time_point timestamp = timestamp_ + start / rx_rate_;

    pkt->timestamp = timestamp;
    pkt->slot_timestamp = timestamp_;
    pkt->start_samples = start;
    pkt->end_samples = end;
    pkt->demod_latency = (now - timestamp).get_real_secs();
    pkt->payload_len = payload.size();

    callback_(std::move(pkt));

    if (snapshot_off_)
        snapshot_collector_->selfTX(*snapshot_off_ + start,
                                    *snapshot_off_ + end,
                                    channel_.fc,
                                    channel_.bw);
}
