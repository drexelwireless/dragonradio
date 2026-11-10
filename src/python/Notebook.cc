// Notebook helpers: packet-level modulate and demodulate for any PHY, so a
// Jupyter kernel running the dragonradio executable as its interpreter can
// pass a packet through the native or liquid PHY with NumPy channels in
// between. Built only with -DDRAGONRADIO_NOTEBOOK=ON (off by default); the
// radio never calls these.
#include <algorithm>
#include <complex>
#include <cstring>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "Clock.hh"
#include "Packet.hh"
#include "phy/PHY.hh"
#include "python/PyModules.hh"

using fc32 = std::complex<float>;

namespace {

/** Restore PHY's process-wide node ID and team on scope exit. */
struct PHYIdentity {
    NodeId node_id = PHY::getNodeId();
    uint8_t team = static_cast<uint8_t>(PHY::getTeam());

    ~PHYIdentity()
    {
        PHY::setNodeId(node_id);
        PHY::setTeam(team);
    }
};

/** Packet start/end/stats as Python sees them. */
struct Received {
    NodeId curhop;
    NodeId nexthop;
    uint16_t seq;
    unsigned mcsidx;
    bool payload_valid;
    std::string data;
    float evm;
    float rssi;
    float cfo;
    size_t start;
    size_t end;
};

py::array_t<fc32> modulate(PHY &phy,
                           py::bytes payload,
                           mcsidx_t mcsidx,
                           NodeId curhop,
                           NodeId nexthop,
                           uint16_t seq,
                           uint8_t team,
                           float gain)
{
    std::string data = payload;
    ModPacket   mpkt;

    {
        py::gil_scoped_release nogil;
        PHYIdentity            saved;

        PHY::setNodeId(curhop);
        PHY::setTeam(team);

        auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + data.size());

        pkt->hdr.curhop = curhop;
        pkt->hdr.nexthop = nexthop;
        pkt->hdr.seq = Seq{seq};
        pkt->hdr.flags.syn = 0;
        pkt->hdr.flags.ack = 0;
        pkt->hdr.flags.has_seq = 1;
        pkt->hdr.flags.has_control = 0;
        pkt->hdr.flags.compressed = 0;
        pkt->hdr.flags.team = 0;
        pkt->ehdr().src = curhop;
        pkt->ehdr().dest = nexthop;
        pkt->ehdr().ack = Seq{0};
        pkt->ehdr().data_len = static_cast<uint16_t>(data.size());
        std::memcpy(pkt->data() + sizeof(ExtendedHeader), data.data(), data.size());
        pkt->mcsidx = mcsidx;

        auto mod = phy.mkPacketModulator();

        mod->modulate(pkt, gain, mpkt);
    }

    py::array_t<fc32> iq(mpkt.nsamples);

    std::memcpy(iq.mutable_data(), mpkt.samples->data() + mpkt.offset, mpkt.nsamples*sizeof(fc32));
    return iq;
}

py::list demodulate(PHY &phy,
                    py::array_t<fc32, py::array::c_style | py::array::forcecast> iq,
                    NodeId node_id,
                    uint8_t team,
                    size_t chunk,
                    double rx_rate)
{
    std::vector<Received> got;
    auto                  buf = iq.request();
    const fc32           *x = static_cast<const fc32*>(buf.ptr);
    const size_t          n = static_cast<size_t>(buf.size);

    {
        py::gil_scoped_release nogil;
        PHYIdentity            saved;

        PHY::setNodeId(node_id);
        PHY::setTeam(team);

        Channel channel(0.0, rx_rate);
        auto    demod = phy.mkPacketDemodulator(0, channel);

        demod->setCallback([&](std::shared_ptr<RadioPacket> &&p) {
            Received r{};

            r.curhop = p->hdr.curhop;
            r.nexthop = p->hdr.nexthop;
            r.seq = static_cast<uint16_t>(p->hdr.seq);
            r.mcsidx = p->mcsidx;
            r.payload_valid = !p->internal_flags.invalid_payload;
            if (r.payload_valid && p->size() >= sizeof(ExtendedHeader)) {
                size_t len = std::min<size_t>(p->ehdr().data_len, p->size() - sizeof(ExtendedHeader));

                r.data.assign(reinterpret_cast<const char*>(p->data() + sizeof(ExtendedHeader)), len);
            }
            r.evm = p->evm;
            r.rssi = p->rssi;
            r.cfo = p->cfo;
            r.start = p->start_samples;
            r.end = p->end_samples;
            got.push_back(std::move(r));
        });
        demod->reset(channel);
        demod->timestamp(MonoClock::time_point{MonoClock::duration(0.0)}, std::nullopt, 0, 0, 1.0f, static_cast<float>(rx_rate));

        if (chunk == 0)
            chunk = n;
        for (size_t i = 0; i < n; i += chunk)
            demod->demodulate(x + i, std::min(chunk, n - i));
    }

    py::list out;

    for (const auto &r : got) {
        py::dict d;

        d["curhop"] = r.curhop;
        d["nexthop"] = r.nexthop;
        d["seq"] = r.seq;
        d["mcsidx"] = r.mcsidx;
        d["payload_valid"] = r.payload_valid;
        d["data"] = py::bytes(r.data);
        d["evm"] = r.evm;
        d["rssi"] = r.rssi;
        d["cfo"] = r.cfo;
        d["start"] = r.start;
        d["end"] = r.end;
        out.append(d);
    }

    return out;
}

}

void exportNotebook(py::module &m)
{
    m.doc() = "Packet-level PHY helpers for notebooks (built with DRAGONRADIO_NOTEBOOK)";

    m.def("modulate",
        modulate,
        "Modulate one packet carrying payload with phy; return its IQ samples",
        py::arg("phy"),
        py::arg("payload"),
        py::arg("mcsidx") = 0,
        py::arg("curhop") = 1,
        py::arg("nexthop") = 2,
        py::arg("seq") = 0,
        py::arg("team") = 0,
        py::arg("gain") = 1.0f);

    m.def("demodulate",
        demodulate,
        "Demodulate IQ samples with phy as node node_id; return one dict per "
        "frame whose header decoded and that was addressed to node_id or broadcast "
        "(keys curhop, nexthop, seq, mcsidx, payload_valid, data, evm, rssi, cfo, "
        "start, end). Frames with an invalid header produce nothing.",
        py::arg("phy"),
        py::arg("iq"),
        py::arg("node_id") = 2,
        py::arg("team") = 0,
        py::arg("chunk") = 0,
        py::arg("rx_rate") = 1e6);
}
