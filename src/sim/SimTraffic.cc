// Synthetic IP traffic in place of the tun/tap device (phase P2).

#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <stdexcept>

#include "logging.hh"
#include "net/mgen.h"
#include "sim/SimTraffic.hh"
#include "util/net.hh"
#include "util/sprintf.hh"

using namespace std::placeholders;

namespace sim {

namespace {

constexpr uint32_t kTrafficMagic = 0x46545244; // "DRTF"
constexpr size_t   kAppHeader = 32;
// Every payload starts with a standard (version 2) MGEN header, as the
// Colosseum traffic the radio was built for does: FlowPerformance and
// Packet::initMGENInfo take the flow ID (= destination port), sequence number
// and send time from it, and mandates are keyed by that flow ID.
constexpr size_t   kMGEN = 20;
constexpr size_t   kMinPayload = kMGEN + kAppHeader;
constexpr size_t   kFrameOverhead = sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr);

uint32_t fnv1a(const uint8_t *p, size_t n, uint32_t h = 2166136261u)
{
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// Hash of a UDP payload, excluding the hash field itself.
uint32_t payloadHash(const uint8_t *p, size_t n)
{
    constexpr size_t h = kMGEN + 28;

    return fnv1a(p + h + 4, n - h - 4, fnv1a(p, h));
}

uint64_t splitmix(uint64_t &x)
{
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);

    z = (z ^ (z >> 30))*0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27))*0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

template <class T>
void put(uint8_t *p, T v)
{
    std::memcpy(p, &v, sizeof(T));
}

template <class T>
T get(const uint8_t *p)
{
    T v;

    std::memcpy(&v, p, sizeof(T));
    return v;
}

uint16_t ipChecksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;

    for (size_t i = 0; i + 1 < n; i += 2)
        sum += (p[i] << 8) | p[i+1];
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return ~sum & 0xffff;
}

int64_t nowNs(void)
{
    auto t = MonoClock::now();

    return t.get_full_secs()*1000000000ll + static_cast<int64_t>(t.get_frac_secs()*1e9);
}

}

SimTraffic::SimTraffic(NodeId node_id,
                       const std::string& ipaddr,
                       const std::string& macaddr,
                       size_t mtu)
  : sink(*this,
         nullptr,
         nullptr,
         std::bind(&SimTraffic::send, this, _1))
  , source(*this,
           nullptr,
           std::bind(&SimTraffic::stop, this))
  , node_id_(node_id)
  , ipaddr_(ipaddr)
  , macaddr_(macaddr)
  , mtu_(mtu)
  , logger_(logger)
  , running_(false)
  , foreign_(0)
{
}

SimTraffic::~SimTraffic()
{
    stop();
}

void SimTraffic::addFlow(uint16_t port, NodeId dest, double rate, size_t size, uint64_t count, double start)
{
    if (size < kMinPayload || size + sizeof(struct ip) + sizeof(struct udphdr) > mtu_)
        throw std::invalid_argument("flow payload must be between 52 bytes and MTU-28");
    if (rate <= 0)
        throw std::invalid_argument("flow rate must be positive");

    std::lock_guard<std::mutex> lock(mutex_);
    Flow                        f;

    if (running_)
        throw std::logic_error("flows must be added before start()");

    f.port = port;
    f.dest = dest;
    f.rate = rate;
    f.size = size;
    f.count = count;
    f.start = start;
    flows_.push_back(f);
}

std::vector<uint8_t> SimTraffic::buildFrame(NodeId dest, uint16_t port, uint64_t seq, size_t size, int64_t tx_ns)
{
    std::vector<uint8_t> frame(kFrameOverhead + size);
    auto                *eth = reinterpret_cast<struct ether_header*>(frame.data());
    uint8_t             *iph = frame.data() + sizeof(struct ether_header);
    uint8_t             *udp = iph + sizeof(struct ip);
    uint8_t             *app = udp + sizeof(struct udphdr);
    bool                 bcast = dest == kNodeBroadcast;

    struct sockaddr hw = parseMAC(sprintf(macaddr_.c_str(), node_id_));

    std::memcpy(eth->ether_shost, hw.sa_data, ETH_ALEN);
    if (bcast)
        std::memset(eth->ether_dhost, 0xff, ETH_ALEN);
    else {
        hw = parseMAC(sprintf(macaddr_.c_str(), dest));
        std::memcpy(eth->ether_dhost, hw.sa_data, ETH_ALEN);
    }
    eth->ether_type = htons(ETHERTYPE_IP);

    struct sockaddr src_ip = parseIP(sprintf(ipaddr_.c_str(), node_id_));
    struct sockaddr dst_ip = parseIP(sprintf(ipaddr_.c_str(), bcast ? 255 : dest));
    uint16_t        iplen = sizeof(struct ip) + sizeof(struct udphdr) + size;

    iph[0] = 0x45;
    put<uint16_t>(iph + 2, htons(iplen));
    put<uint16_t>(iph + 4, htons(static_cast<uint16_t>(seq)));
    iph[8] = 64;
    iph[9] = IPPROTO_UDP;
    std::memcpy(iph + 12, &reinterpret_cast<struct sockaddr_in*>(&src_ip)->sin_addr, 4);
    std::memcpy(iph + 16, &reinterpret_cast<struct sockaddr_in*>(&dst_ip)->sin_addr, 4);
    put<uint16_t>(iph + 10, htons(ipChecksum(iph, sizeof(struct ip))));

    put<uint16_t>(udp, htons(port));
    put<uint16_t>(udp + 2, htons(port));
    put<uint16_t>(udp + 4, htons(sizeof(struct udphdr) + size));

    uint8_t *mgen = app;

    app = mgen + kMGEN;
    {
        WallClock::time_point wall = WallClock::now();

        put<uint16_t>(mgen, htons(static_cast<uint16_t>(size)));        // messageSize
        mgen[2] = MGEN_VERSION;
        mgen[3] = MGEN_CLEAR;
        put<uint32_t>(mgen + 4, htonl(port));                           // mgenFlowId
        put<uint32_t>(mgen + 8, htonl(static_cast<uint32_t>(seq)));     // sequenceNumber
        put<uint32_t>(mgen + 12, htonl(static_cast<uint32_t>(wall.get_full_secs())));
        put<uint32_t>(mgen + 16, htonl(static_cast<uint32_t>(wall.get_frac_secs()*1e6)));
    }

    put<uint32_t>(app, kTrafficMagic);
    app[4] = node_id_;
    app[5] = dest;
    put<uint16_t>(app + 6, port);
    put<uint64_t>(app + 8, seq);
    put<int64_t>(app + 16, tx_ns);
    put<uint32_t>(app + 24, static_cast<uint32_t>(size));

    uint64_t state = seq*0x100000001b3ull ^ (static_cast<uint64_t>(port) << 48) ^ node_id_;

    for (size_t i = kMinPayload; i < size; i += 8) {
        uint64_t r = splitmix(state);

        std::memcpy(mgen + i, &r, std::min<size_t>(8, size - i));
    }

    put<uint32_t>(app + 28, payloadHash(mgen, size));

    return frame;
}

void SimTraffic::start(void)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_)
        return;

    running_ = true;
    worker_ = std::thread(&SimTraffic::worker, this);
}

void SimTraffic::stop(void)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        running_ = false;
    }

    cond_.notify_all();

    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
        worker_.join();
}

void SimTraffic::worker(void)
{
    using clock = std::chrono::steady_clock;

    clock::time_point     t0 = clock::now();
    std::vector<uint64_t> next;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        next.assign(flows_.size(), 0);
    }

    for (;;) {
        // Find the earliest pending transmission across flows.
        std::unique_lock<std::mutex> lock(mutex_);
        ssize_t                      best = -1;
        clock::time_point            t_best;

        for (size_t i = 0; i < flows_.size(); ++i) {
            const Flow &f = flows_[i];

            if (f.count != 0 && next[i] >= f.count)
                continue;

            auto t = t0 + std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(f.start + next[i]/f.rate));

            if (best < 0 || t < t_best) {
                best = i;
                t_best = t;
            }
        }

        if (best < 0) {
            cond_.wait(lock, [this]{ return !running_; });
            return;
        }

        if (cond_.wait_until(lock, t_best, [this]{ return !running_; }))
            return;

        Flow                &f = flows_[best];
        uint64_t             seq = next[best]++;
        std::vector<uint8_t> frame = buildFrame(f.dest, f.port, seq, f.size, nowNs());

        f.sent++;
        lock.unlock();

        auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + frame.size());

        std::memcpy(pkt->data() + sizeof(ExtendedHeader), frame.data(), frame.size());
        pkt->hdr.flags.has_seq = 1;
        pkt->ehdr().data_len = frame.size();
        pkt->timestamp = MonoClock::now();
        pkt->tuntap_timestamp = WallClock::to_wall_time(pkt->timestamp);
        source.push(std::move(pkt));
    }
}

void SimTraffic::send(std::shared_ptr<RadioPacket>&& pkt)
{
    int64_t        t_rx = nowNs();
    const uint8_t *frame = pkt->data() + sizeof(ExtendedHeader);
    size_t         len = pkt->ehdr().data_len;

    pkt->tuntap_timestamp = MonoClock::now();

    if (logger_ && logger_->getCollectSource(Logger::kRecvPackets))
        logger_->logRecv(pkt);

    std::lock_guard<std::mutex> lock(mutex_);

    if (len < kFrameOverhead + kMinPayload
        || ntohs(get<uint16_t>(frame + 12)) != ETHERTYPE_IP
        || frame[sizeof(struct ether_header) + 9] != IPPROTO_UDP) {
        foreign_++;
        return;
    }

    const uint8_t *payload = frame + kFrameOverhead;
    const uint8_t *app = payload + kMGEN;
    size_t         size = len - kFrameOverhead;

    if (get<uint32_t>(app) != kTrafficMagic) {
        foreign_++;
        return;
    }

    auto       key = std::make_pair(static_cast<NodeId>(app[4]), get<uint16_t>(app + 6));
    RecvStats &st = recv_[key];

    if (get<uint32_t>(app + 24) != size || get<uint32_t>(app + 28) != payloadHash(payload, size)) {
        st.corrupt++;
        return;
    }

    uint64_t seq = get<uint64_t>(app + 8);

    if (!seen_[key].insert(seq).second) {
        st.duplicates++;
        st.dup_seqs.push_back(seq);
        return;
    }

    if (!st.seqs.empty() && seq < st.max_seq)
        st.out_of_order++;

    st.max_seq = std::max(st.max_seq, seq);
    if (st.unique == 0)
        st.first_rx = t_rx*1e-9;
    st.last_rx = t_rx*1e-9;
    st.unique++;
    st.bytes += size;
    st.seqs.push_back(seq);
    st.arrivals.push_back(t_rx*1e-9);
    st.latency.push_back((t_rx - get<int64_t>(app + 16))*1e-9);
}

std::vector<SimTraffic::Flow> SimTraffic::getFlows(void)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return flows_;
}

std::map<std::pair<NodeId, uint16_t>, SimTraffic::RecvStats> SimTraffic::getReceived(void)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return recv_;
}

uint64_t SimTraffic::getForeign(void)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return foreign_;
}

}
