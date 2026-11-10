// Python binding for the host sensing-delay probe (probe/SenseDelay.hh).

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "probe/SenseDelay.hh"
#include "python/PyModules.hh"

void exportProbe(py::module &m)
{
    m.def("runSenseDelay",
        [](std::shared_ptr<USRP> usrp,
           const std::string &mode,
           double duration,
           size_t window,
           double threshold_db,
           double threshold,
           double noise_secs,
           double lead,
           double period,
           double holdoff,
           size_t burst_len,
           float amplitude,
           double cal_interval)
        {
            probe::SenseDelayConfig c;

            if (mode == "rx")
                c.mode = probe::Mode::kReceive;
            else if (mode == "react")
                c.mode = probe::Mode::kReact;
            else if (mode == "ping")
                c.mode = probe::Mode::kPing;
            else
                throw std::invalid_argument("mode must be rx, react or ping");

            c.window = window;
            c.threshold_db = threshold_db;
            c.threshold = threshold;
            c.noise_secs = noise_secs;
            c.lead = lead;
            c.period = period;
            c.holdoff = holdoff;
            c.burst_len = burst_len;
            c.amplitude = amplitude;
            c.cal_interval = cal_interval;

            probe::SenseDelayResults r;

            {
                py::gil_scoped_release release;
                probe::SenseDelayProbe p(usrp, c);

                r = p.run(duration);
            }

            py::dict d;

            d["chunk_dev_end"] = r.chunk_dev_end;
            d["chunk_host"] = r.chunk_host;
            d["chunk_n"] = r.chunk_n;
            d["cal_host"] = r.cal_host;
            d["cal_offset"] = r.cal_offset;
            d["cal_rtt"] = r.cal_rtt;
            d["edge_dev"] = r.edge_dev;
            d["edge_host"] = r.edge_host;
            d["edge_power_db"] = r.edge_power_db;
            d["react_edge_dev"] = r.react_edge_dev;
            d["react_tx_dev"] = r.react_tx_dev;
            d["react_host_trigger"] = r.react_host_trigger;
            d["react_host_sent"] = r.react_host_sent;
            d["ping_tx_dev"] = r.ping_tx_dev;
            d["ping_host_sent"] = r.ping_host_sent;
            d["noise_power"] = r.noise_power;
            d["threshold"] = r.threshold;
            d["tx_late"] = r.tx_late;
            d["tx_underflow"] = r.tx_underflow;
            d["rx_errors"] = r.rx_errors;
            d["chunk_samps"] = r.chunk_samps;
            d["rx_rate"] = r.rx_rate;
            return d;
        },
        py::arg("usrp"),
        py::arg("mode") = "rx",
        py::arg("duration") = 10.0,
        py::arg("window") = 32,
        py::arg("threshold_db") = 10.0,
        py::arg("threshold") = 0.0,
        py::arg("noise_secs") = 0.5,
        py::arg("lead") = 5e-3,
        py::arg("period") = 0.05,
        py::arg("holdoff") = 0.025,
        py::arg("burst_len") = 256,
        py::arg("amplitude") = 0.5f,
        py::arg("cal_interval") = 1.0,
        "Run the host sensing-delay probe (docs/sensing-delay-measurement.rst) "
        "and return its raw measurements");
}
