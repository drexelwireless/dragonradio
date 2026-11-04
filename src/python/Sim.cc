// Python bindings for full-stack simulation (phase P2).

#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/complex.h>
#include <pybind11/stl.h>

#include "logging.hh"
#include "sim/RxRecorder.hh"
#include "sim/SimTraffic.hh"
#include "sim/SimUSRP.hh"
#include "python/PyModules.hh"

void exportSim(py::module &m)
{
    using sim::SimUSRP;
    using sim::SimTraffic;

    // A Radio, as USRP is, so the MACs take it unchanged; the USRP-only
    // properties the controller reads (clock rate and sources, max samps)
    // have simulated equivalents. Antennas and subdevices do not exist.
    py::class_<SimUSRP, Radio, MonoClock::TimeKeeper, std::shared_ptr<SimUSRP>>(m, "SimUSRP")
        .def(py::init<const std::string&, uint8_t, double, double, double, uint64_t>(),
             py::arg("air_path"),
             py::arg("node_id"),
             py::arg("freq"),
             py::arg("clock_rate") = 200e6,
             py::arg("rx_latency") = 2e-3,
             py::arg("seed") = 1,
             "Simulated radio device connected to an air relay",
             py::call_guard<py::gil_scoped_release>())
        .def_property_readonly("node_id",
            &SimUSRP::getNodeId)
        .def_property_readonly("clock_rate_ranges",
            [](SimUSRP &self)
            {
                return std::vector<std::pair<double, double>>{{self.getMasterClockRate(), self.getMasterClockRate()}};
            })
        .def_property("clock_rate",
            &SimUSRP::getMasterClockRate,
            [](SimUSRP &self, double rate)
            {
                if (rate != self.getMasterClockRate())
                    throw std::invalid_argument("simulated device has a fixed master clock rate");
            })
        .def_property_readonly("clock_sources",
            [](SimUSRP &self) { return std::vector<std::string>{"internal"}; })
        .def_property("clock_source",
            [](SimUSRP &self) { return std::string("internal"); },
            [](SimUSRP &self, const std::string &src)
            {
                logUSRP(LOGDEBUG, "Simulated device ignores clock source %s", src.c_str());
            })
        .def_property_readonly("time_sources",
            [](SimUSRP &self) { return std::vector<std::string>{"internal"}; })
        .def_property("time_source",
            [](SimUSRP &self) { return std::string("internal"); },
            [](SimUSRP &self, const std::string &src)
            {
                logUSRP(LOGDEBUG, "Simulated device ignores time source %s", src.c_str());
            })
        .def("syncTime",
            [](SimUSRP &self, double random_bias, bool use_pps)
            {
                // The device clock is the host clock (with setClock's offset
                // and rate error); there is nothing to set and no PPS.
                logUSRP(LOGDEBUG, "Simulated device: syncTime is a no-op");
            },
            py::arg("random_bias")=0.0,
            py::arg("use_pps")=false)
        .def("setClock",
            &SimUSRP::setClock,
            py::arg("offset"),
            py::arg("ppm"),
            "Give the device clock an offset (sec) and rate error (ppm); set before it becomes the time keeper")
        .def_property_readonly("simulated_clock",
            [](SimUSRP &self) { return py::make_tuple(self.getClockOffset(), self.getClockPPM()); },
            "(offset, ppm) of the simulated device clock")
        .def("hostToDevice",
            &SimUSRP::hostToDevice,
            "Device time (sec) of a host CLOCK_MONOTONIC time (sec)")
        .def("deviceToHost",
            &SimUSRP::deviceToHost,
            "Host CLOCK_MONOTONIC time (sec) of a device time (sec)")
        .def_property("auto_dc_offset",
            &SimUSRP::getAutoDCOffset,
            &SimUSRP::setAutoDCOffset)
        .def_property("tx_max_samps",
            &SimUSRP::getMaxTXSamps,
            &SimUSRP::setMaxTXSamps)
        .def_property("tx_max_samps_factor",
            nullptr,
            &SimUSRP::setMaxTXSampsFactor)
        .def_property("rx_max_samps",
            &SimUSRP::getMaxRXSamps,
            &SimUSRP::setMaxRXSamps)
        .def_property("rx_max_samps_factor",
            nullptr,
            &SimUSRP::setMaxRXSampsFactor)
        .def("stop",
            &SimUSRP::stop,
            py::call_guard<py::gil_scoped_release>())
        .def("setLink",
            [](SimUSRP &self, uint8_t src, double gain_db, double cfo_hz, int64_t delay, double erasure, int64_t jitter,
               double sfo_ppm)
            {
                sim::Link link;

                link.sfo_ppm = sfo_ppm;
                link.gain_db = gain_db;
                link.cfo_hz = cfo_hz;
                link.delay = delay;
                link.erasure = erasure;
                link.jitter = jitter;
                self.setLink(src, link);
            },
            py::arg("src"),
            py::arg("gain_db") = 0.0,
            py::arg("cfo_hz") = 0.0,
            py::arg("delay") = 0,
            py::arg("erasure") = 0.0,
            py::arg("jitter") = 0,
            py::arg("sfo_ppm") = 0.0,
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
                d["jitter"] = link.jitter;
                d["sfo_ppm"] = link.sfo_ppm;
                return d;
            })
        .def("setImpairments",
            [](SimUSRP &self, bool gain_model, double tx_gain_ref_db, double rx_gain_ref_db, double adc_full_scale,
               double iq_amp_db, double iq_phase_deg, std::complex<float> dc_offset, std::optional<double> tx_leakage_db)
            {
                sim::Impairments imp;

                if (tx_leakage_db)
                    imp.tx_leakage_db = *tx_leakage_db;
                imp.gain_model = gain_model;
                imp.tx_gain_ref_db = tx_gain_ref_db;
                imp.rx_gain_ref_db = rx_gain_ref_db;
                imp.adc_full_scale = adc_full_scale;
                imp.iq_amp_db = iq_amp_db;
                imp.iq_phase_deg = iq_phase_deg;
                imp.dc = dc_offset;
                self.setImpairments(imp);
            },
            py::arg("gain_model") = false,
            py::arg("tx_gain_ref_db") = 0.0,
            py::arg("rx_gain_ref_db") = 0.0,
            py::arg("adc_full_scale") = 0.0,
            py::arg("iq_amp_db") = 0.0,
            py::arg("iq_phase_deg") = 0.0,
            py::arg("dc_offset") = std::complex<float>(0.0f, 0.0f),
            py::arg("tx_leakage_db") = std::nullopt,
            "Set hardware-like gain, IQ imbalance, DC offset and ADC clipping (all off by default)")
        .def_property_readonly("impairments",
            [](SimUSRP &self)
            {
                sim::Impairments imp = self.getImpairments();
                py::dict         d;

                d["gain_model"] = imp.gain_model;
                d["tx_gain_ref_db"] = imp.tx_gain_ref_db;
                d["rx_gain_ref_db"] = imp.rx_gain_ref_db;
                d["adc_full_scale"] = imp.adc_full_scale;
                d["iq_amp_db"] = imp.iq_amp_db;
                d["iq_phase_deg"] = imp.iq_phase_deg;
                d["dc_offset"] = imp.dc;
                d["tx_leakage_db"] = std::isnan(imp.tx_leakage_db) ? py::none() : py::cast(imp.tx_leakage_db);
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
                d["rx_clipped"] = s.rx_clipped;
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
        .def_property("detail",
            &SimTraffic::getDetail,
            &SimTraffic::setDetail,
            "Keep per-packet records (True) or bounded summaries (False, for soak runs)")
        .def_property_readonly_static("latency_bins",
            [](py::object)
            {
                return py::make_tuple(SimTraffic::kLatencyMin, SimTraffic::kLatencyRatio, SimTraffic::kLatencyBins);
            })
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
                    e["arrivals"] = st.arrivals;
                    e["latency"] = st.latency;
                    e["seqs"] = st.seqs;
                    e["dup_seqs"] = st.dup_seqs;
                    e["latency_sum"] = st.latency_sum;
                    e["latency_min"] = st.latency_min;
                    e["latency_max"] = st.latency_max;
                    e["latency_hist"] = st.latency_hist;
                    e["per_second"] = st.per_second;
                    e["stale"] = st.stale;
                    d[py::make_tuple(key.first, key.second)] = e;
                }
                return d;
            })
        .def_property_readonly("foreign", &SimTraffic::getForeign)
        ;

    py::class_<sim::RxRecorder, RadioProcessor, std::shared_ptr<sim::RxRecorder>>(m, "RxRecorder")
        .def(py::init<>())
        .def_property("keep_records",
            &sim::RxRecorder::getKeepRecords,
            &sim::RxRecorder::setKeepRecords,
            "Keep one record per frame (True) or only per-source counts (False)")
        .def_property_readonly("counts",
            [](sim::RxRecorder &self)
            {
                py::dict d;

                for (const auto &[src, c] : self.getCounts()) {
                    py::dict e;

                    e["frames"] = c.frames;
                    e["header_valid"] = c.header_valid;
                    e["payload_valid"] = c.payload_valid;
                    e["data_valid"] = c.data_valid;
                    d[py::int_(src)] = e;
                }
                return d;
            })
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
