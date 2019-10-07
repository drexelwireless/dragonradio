#include "phy/Modem.hh"
#include "python/PyModules.hh"

void exportMCS(py::module &m)
{
    // Export class MCS to Python
    py::class_<MCS, std::shared_ptr<MCS>>(m, "MCS")
    .def(py::init<>())
    .def(py::init<crc_scheme,
                  fec_scheme,
                  fec_scheme,
                  modulation_scheme>())
    .def_readwrite("check", &MCS::check, "Data validity check")
    .def_readwrite("fec0", &MCS::fec0, "Inner FEC")
    .def_readwrite("fec1", &MCS::fec1, "Outer FEC")
    .def_readwrite("ms", &MCS::ms, "Modulation scheme")
    .def_property_readonly("rate", &MCS::getRate, "Approximate rate (bps)")
    .def("__repr__", [](const MCS& self) {
        return py::str("MCS(check={}, fec0={}, fec1={}, ms={})").format(self.check, self.fec0, self.fec1, self.ms);
     })
    ;
}
