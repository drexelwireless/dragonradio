#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "CIL.hh"
#include "python/PyModules.hh"

PYBIND11_MAKE_OPAQUE(MandateMap)

void exportCIL(py::module &m)
{
    // Export Mandate class to Python
    py::class_<Mandate, std::unique_ptr<Mandate>>(m, "Mandate")
    .def(py::init())
    .def(py::init<FlowUID,
                  double,
                  int,
                  std::optional<double>,
                  std::optional<double>,
                  std::optional<double>>())
    .def_readonly("flow_uid",
        &Mandate::flow_uid,
        "Flow UID)")
    .def_readonly("hold_period",
        &Mandate::hold_period,
        "Steady state period required for mandate success (sec)")
    .def_readonly("point_value",
        &Mandate::point_value,
        "Point value")
    .def_readonly("max_latency_s",
        &Mandate::max_latency_s,
        "Maximum latency allowed for a packet (sec)")
    .def_readonly("min_throughput_bps",
        &Mandate::min_throughput_bps,
        "Minimum throughput (bps)")
    .def_readonly("file_transfer_deadline_s",
        &Mandate::file_transfer_deadline_s,
        "File transfer delivery deadline (sec)")
    ;

    py::bind_map<MandateMap>(m, "MandateMap");
}
