// Python bindings for full-stack simulation (phase P2).

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "sim/RxRecorder.hh"
#include "sim/SimTraffic.hh"
#include "sim/SimUSRP.hh"
#include "python/PyModules.hh"

void exportSim(py::module &m)
{
    using sim::SimUSRP;
    using sim::SimTraffic;

    py::class_<SimUSRP, USRP, std::shared_ptr<SimUSRP>>(m, "SimUSRP")
        .def(py::init<const std::string&, uint8_t, double, double, double, uint64_t>(),
             py::arg("air_path"),
             py::arg("node_id"),
             py::arg("freq"),
             py::arg("clock_rate") = 200e6,
             py::arg("rx_latency") = 2e-3,
             py::arg("seed") = 1,
             "Simulated radio device connected to an air relay")
        .def("setLink",
            [](SimUSRP &self, uint8_t src, double gain_db, double cfo_hz, int64_t delay, double erasure)
            {
                sim::Link link;

                link.gain_db = gain_db;
                link.cfo_hz = cfo_hz;
                link.delay = delay;
                link.erasure = erasure;
                self.setLink(src, link);
            },
            py::arg("src"),
            py::arg("gain_db") = 0.0,
            py::arg("cfo_hz") = 0.0,
            py::arg("delay") = 0,
            py::arg("erasure") = 0.0,
            "Set the directed link model from node src to this node")
        .def("getLink",
            [](SimUSRP &self, uint8_t src)
            {
                sim::Link link = self.getLink(src);
                py::dict  d;

                d["gain_db"] = link.gain_db;
                d["cfo_hz"] = link.cfo_hz;
                d["delay"] = link.delay;
                d["erasure"] = link.erasure;
                return d;
            })
        .def_property("noise_power",
            &SimUSRP::getNoisePower,
            &SimUSRP::setNoisePower,
            "Receiver noise power per complex sample (dB)")
        .def_property_readonly("stats",
            [](SimUSRP &self)
            {
                sim::Stats s = self.getStats();
                py::dict   d;

                d["tx_bursts"] = s.tx_bursts;
                d["tx_samples"] = s.tx_samples;
                d["tx_late"] = s.tx_late;
                d["tx_underflow"] = s.tx_underflow;
                d["rx_bursts"] = s.rx_bursts;
                d["rx_erased"] = s.rx_erased;
                d["rx_late"] = s.rx_late;
                d["rx_partial_late"] = s.rx_partial_late;
                d["rx_rate_mismatch"] = s.rx_rate_mismatch;
                d["rx_queue_drops"] = s.rx_queue_drops;
                d["rx_samples"] = s.rx_samples;
                d["rx_overflows"] = s.rx_overflows;
                d["rx_overflow_samples"] = s.rx_overflow_samples;
                d["rx_streams"] = s.rx_streams;
                return d;
            })
        ;

    py::class_<SimTraffic, std::shared_ptr<SimTraffic>>(m, "SimTraffic")
        .def(py::init<NodeId, const std::string&, const std::string&, size_t>(),
             py::arg("node_id"),
             py::arg("ipaddr"),
             py::arg("macaddr"),
             py::arg("mtu"))
        .def_property_readonly("source", [](std::shared_ptr<SimTraffic> element) { return exposePort(element, &element->source); } )
        .def_property_readonly("sink", [](std::shared_ptr<SimTraffic> element) { return exposePort(element, &element->sink); } )
        .def("addFlow",
            &SimTraffic::addFlow,
            py::arg("port"),
            py::arg("dest"),
            py::arg("rate"),
            py::arg("size"),
            py::arg("count") = 0,
            py::arg("start") = 0.0)
        .def("start", &SimTraffic::start, py::call_guard<py::gil_scoped_release>())
        .def("stop", &SimTraffic::stop, py::call_guard<py::gil_scoped_release>())
        .def("buildFrame",
            [](SimTraffic &self, NodeId dest, uint16_t port, uint64_t seq, size_t size, int64_t tx_ns)
            {
                auto f = self.buildFrame(dest, port, seq, size, tx_ns);

                return py::bytes(reinterpret_cast<const char*>(f.data()), f.size());
            })
        .def_property_readonly("flows",
            [](SimTraffic &self)
            {
                py::list l;

                for (const auto &f : self.getFlows()) {
                    py::dict d;

                    d["port"] = f.port;
                    d["dest"] = f.dest;
                    d["rate"] = f.rate;
                    d["size"] = f.size;
                    d["count"] = f.count;
                    d["start"] = f.start;
                    d["sent"] = f.sent;
                    l.append(d);
                }
                return l;
            })
        .def_property_readonly("received",
            [](SimTraffic &self)
            {
                py::dict d;

                for (const auto &[key, st] : self.getReceived()) {
                    py::dict e;

                    e["unique"] = st.unique;
                    e["duplicates"] = st.duplicates;
                    e["corrupt"] = st.corrupt;
                    e["out_of_order"] = st.out_of_order;
                    e["max_seq"] = st.max_seq;
                    e["bytes"] = st.bytes;
                    e["first_rx"] = st.first_rx;
                    e["last_rx"] = st.last_rx;
                    e["latency"] = st.latency;
                    e["seqs"] = st.seqs;
                    e["dup_seqs"] = st.dup_seqs;
                    d[py::make_tuple(key.first, key.second)] = e;
                }
                return d;
            })
        .def_property_readonly("foreign", &SimTraffic::getForeign)
        ;

    py::class_<sim::RxRecorder, RadioProcessor, std::shared_ptr<sim::RxRecorder>>(m, "RxRecorder")
        .def(py::init<>())
        .def_property_readonly("records",
            [](sim::RxRecorder &self)
            {
                py::list l;

                for (const auto &r : self.getRecords())
                    l.append(py::make_tuple(r.timestamp, r.curhop, r.nexthop, r.seq, r.flags,
                                            r.header_valid, r.payload_valid, r.data_len,
                                            r.mcsidx, r.evm, r.rssi, r.cfo));
                return l;
            },
            "(timestamp, curhop, nexthop, seq, flags, header_valid, payload_valid, data_len, mcsidx, evm, rssi, cfo)")
        ;
}
