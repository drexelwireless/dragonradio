// Python binding for the native (non-liquidDSP) OFDM PHY.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "phy/NativeOFDM.hh"
#include "phy/NativeOFDMA.hh"
#include "python/PyModules.hh"

void exportNativePHYs(py::module &m)
{
    // Export class NativeOFDM to Python
    py::class_<NativeOFDM, PHY, std::shared_ptr<NativeOFDM>>(m, "NativeOFDM")
        .def(py::init<const std::vector<std::pair<liquid::MCS, AutoGain>>&,
                      unsigned int,
                      unsigned int,
                      const std::optional<std::string>&,
                      unsigned int,
                      unsigned int,
                      bool,
                      const std::string&>(),
             py::arg("mcs_table"),
             py::arg("M"),
             py::arg("cp_len"),
             py::arg("subcarriers") = std::nullopt,
             py::arg("max_payload") = 4096,
             py::arg("chase_bits") = 4,
             py::arg("soft_payload") = true,
             py::arg("header_fec") = "none")
        .def_property_readonly("M", &NativeOFDM::getM)
        .def_property_readonly("cp_len", &NativeOFDM::getCPLen)
        .def_property_readonly("max_payload", &NativeOFDM::getMaxPayload)
        .def_property_readonly("chase_bits", &NativeOFDM::getChaseBits)
        .def_property_readonly("subcarriers", &NativeOFDM::getSubcarriers)
        .def_property_readonly("bits_per_symbol", &NativeOFDM::getBitsPerSymbol)
        .def_property_readonly("fec", &NativeOFDM::getFEC, "(inner, outer) FEC code names per MCS index")
        .def_property_readonly("soft_payload", &NativeOFDM::getSoftPayload)
        .def_property_readonly("header_fec", &NativeOFDM::getHeaderFEC)
        .def_property_readonly_static("fec_available", [](py::object) { return NativeOFDM::fecAvailable(); })
        ;

    // Export class NativeOFDMA (P5 downlink OFDMA) to Python
    py::class_<NativeOFDMA, NativeOFDM, std::shared_ptr<NativeOFDMA>>(m, "NativeOFDMA")
        .def(py::init<const std::vector<std::pair<liquid::MCS, AutoGain>>&,
                      unsigned int,
                      unsigned int,
                      const std::optional<std::string>&,
                      unsigned int,
                      bool,
                      const std::string&>(),
             py::arg("mcs_table"),
             py::arg("M"),
             py::arg("cp_len"),
             py::arg("subcarriers") = std::nullopt,
             py::arg("max_payload") = 4096,
             py::arg("soft_payload") = true,
             py::arg("allocation") = "equal")
        .def_property_readonly("allocation", &NativeOFDMA::getAllocation)
        .def_property_readonly("max_burst_users", &NativeOFDMA::getMaxBurstUsers)
        ;
}
