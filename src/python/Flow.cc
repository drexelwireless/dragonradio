#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "net/Firewall.hh"
#include "net/FlowInfo.hh"
#include "net/FlowSource.hh"
#include "net/FlowSink.hh"
#include "net/MandatedOutcome.hh"
#include "python/PyModules.hh"

PYBIND11_MAKE_OPAQUE(FlowInfoMap)

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

    // Export FlowInfo class to Python
    py::class_<FlowInfo, std::unique_ptr<FlowInfo>>(m, "FlowInfo")
        .def_readonly("src",
            &FlowInfo::src,
            "Flow source")
        .def_readonly("dest",
            &FlowInfo::dest,
            "Flow destination")
        .def_readonly("latency",
            &FlowInfo::latency,
            "Flow latency (sec)")
        .def_readonly("min_latency",
            &FlowInfo::min_latency,
            "Flow minimum latency (sec)")
        .def_readonly("max_latency",
            &FlowInfo::max_latency,
            "Flow maximum latency (sec)")
        .def_readonly("throughput",
            &FlowInfo::throughput,
            "Flow throughput (bps)")
        .def_readonly("bytes",
            &FlowInfo::bytes,
            "Bytes transmitted")
        .def("__repr__", [](const FlowInfo& self) {
            return py::str("FlowInfo(src={}, dest={}, latency={}, throughput={}, bytes={})").\
            format(self.src, self.dest, self.latency.getValue(), self.throughput.getValue(), self.bytes);
         })
        ;

    py::bind_map<FlowInfoMap>(m, "FlowInfoMap");

    // Export class FlowSource to Python
    py::class_<FlowSource, NetProcessor, std::shared_ptr<FlowSource>>(m, "FlowSource")
        .def(py::init<double>())
        .def_property("mp",
            &FlowSource::getMeasurementPeriod,
            &FlowSource::setMeasurementPeriod,
            "Measurement period (sec)")
        .def_property("mandates",
            &FlowSource::getMandates,
            &FlowSource::setMandates,
            "Mandates")
        .def_property_readonly("flows",
            &FlowSource::getFlowInfo,
            "Return set of observed flows")
        ;

    // Export class FlowSink to Python
    py::class_<FlowSink, RadioProcessor, std::shared_ptr<FlowSink>>(m, "FlowSink")
        .def(py::init<double>())
        .def_property("mp",
            &FlowSink::getMeasurementPeriod,
            &FlowSink::setMeasurementPeriod,
            "Measurement period (sec)")
        .def_property("mandates",
            &FlowSink::getMandates,
            &FlowSink::setMandates,
            "Mandates")
        .def_property_readonly("flows",
            &FlowSink::getFlowInfo,
            "Return set of observed flows")
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
