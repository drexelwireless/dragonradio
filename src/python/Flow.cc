#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "net/Firewall.hh"
#include "net/FlowPerformance.hh"
#include "net/MandatedOutcome.hh"
#include "python/PyModules.hh"

PYBIND11_MAKE_OPAQUE(MandatedOutcomeMap)

void exportFlow(py::module &m)
{
    // Export MandatedOutcome class to Python
    py::class_<MandatedOutcome, std::unique_ptr<MandatedOutcome>>(m, "MandatedOutcome")
    .def(py::init())
    .def(py::init<double,
                  double,
                  int,
                  std::optional<double>,
                  std::optional<double>,
                  std::optional<double>>())
    .def_readwrite("steady_state_period",
        &MandatedOutcome::steady_state_period,
        "Steady state period required for mandate success (sec)")
    .def_readwrite("max_drop_rate",
        &MandatedOutcome::max_drop_rate,
        "Maximum drop rate as a fraction of traffic")
    .def_readwrite("point_value",
        &MandatedOutcome::point_value,
        "Point value")
    .def_readwrite("min_throughput_bps",
        &MandatedOutcome::min_throughput_bps,
        "Minimum throughput (bps)")
    .def_readwrite("max_latency_sec",
        &MandatedOutcome::max_latency_sec,
        "Maximum latency allowed for a packet (sec)")
    .def_readwrite("deadline",
        &MandatedOutcome::deadline,
        "Delivery deadline (sec)")
    ;

    py::bind_map<MandatedOutcomeMap>(m, "MandatedOutcomeMap");

    // Export MPStats class to Python
    py::class_<MPStats, std::unique_ptr<MPStats>>(m, "MPStats")
    .def_readonly("npackets",
        &MPStats::npackets,
        "Number of packets")
    .def_readonly("nbytes",
        &MPStats::nbytes,
        "Number of bytes")
    .def("__repr__", [](const MPStats& self) {
        return py::str("MPStats(npackets={}, nbytes={})").\
        format(self.nbytes, self.nbytes);
     })
    ;

    py::bind_vector<std::vector<MPStats>>(m, "MPStatsVector");

    // Export FlowStats class to Python
    py::class_<FlowStats, std::unique_ptr<FlowStats>>(m, "FlowStats")
    .def_readonly("flow_uid",
        &FlowStats::flow_uid,
        "Flow UID")
    .def_readonly("src",
        &FlowStats::src,
        "Flow source")
    .def_readonly("dest",
        &FlowStats::dest,
        "Flow destinations")
    .def_readonly("low_mp",
        &FlowStats::low_mp,
        "Lowest MP modified")
    .def_readonly("stats",
        &FlowStats::stats,
        "Flow statistics per-measuremnt period")
    .def("__repr__", [](const FlowStats& self) {
        return py::str("FlowStats(src={}, dest={})").\
        format(self.src, self.dest);
     })
    ;

    // Export class FlowPerformance to Python
    py::class_<FlowPerformance, std::shared_ptr<FlowPerformance>>(m, "FlowPerformance")
        .def(py::init<double>())
        .def_property_readonly("mp",
            &FlowPerformance::getMeasurementPeriod,
            "Measurement period (sec)")
        .def_property("start",
            &FlowPerformance::getStart,
            &FlowPerformance::setStart,
            "Match start time in seconds since epoch")
        .def_property("mandates",
            &FlowPerformance::getMandates,
            &FlowPerformance::setMandates,
            "Mandates")
        .def("getSources",
            &FlowPerformance::getSources,
            "Get flow source statistics")
        .def("getSinks",
            &FlowPerformance::getSinks,
            "Get flow sink statistics")
        .def_property_readonly("net_in",
            [](std::shared_ptr<FlowPerformance> element) { return exposePort(element, &element->net_in); },
            "Network packet input port")
        .def_property_readonly("net_out",
            [](std::shared_ptr<FlowPerformance> element) { return exposePort(element, &element->net_out); },
            "Network packet output port")
        .def_property_readonly("radio_in",
            [](std::shared_ptr<FlowPerformance> element) { return exposePort(element, &element->radio_in); },
            "Radio packet input port")
        .def_property_readonly("radio_out",
            [](std::shared_ptr<FlowPerformance> element) { return exposePort(element, &element->radio_out); },
            "Radio packet output port")
        ;

    py::class_<NetFirewall, NetProcessor, std::shared_ptr<NetFirewall>>(m, "NetFirewall")
        .def(py::init<>())
        .def_property("enabled",
            &NetFirewall::getEnabled,
            &NetFirewall::setEnabled,
            "Is the firewall enabled?")
        .def_property("allow_broadcasts",
            &NetFirewall::getAllowBroadcasts,
            &NetFirewall::setAllowBroadcasts,
            "Allow broadcast packets?")
        .def_property("allowed",
            &NetFirewall::getAllowedPorts,
            &NetFirewall::setAllowedPorts,
            "Set of allowed ports")
        ;

    py::class_<RadioFirewall, RadioProcessor, std::shared_ptr<RadioFirewall>>(m, "RadioFirewall")
        .def(py::init<>())
        .def_property("enabled",
            &RadioFirewall::getEnabled,
            &RadioFirewall::setEnabled,
            "Is the firewall enabled?")
        .def_property("allow_broadcasts",
            &RadioFirewall::getAllowBroadcasts,
            &RadioFirewall::setAllowBroadcasts,
            "Allow broadcast packets?")
        .def_property("allowed",
            &RadioFirewall::getAllowedPorts,
            &RadioFirewall::setAllowedPorts,
            "Set of allowed ports")
        ;
}
