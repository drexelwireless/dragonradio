// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/stl.h>

#include "USRP.hh"
#include "python/PyModules.hh"

void exportUSRP(py::module &m)
{
    // Export class USRP to Python
    py::enum_<USRP::DeviceType>(m, "DeviceType")
        .value("N210", USRP::kUSRPN210)
        .value("X310", USRP::kUSRPX310)
        .value("Unknown", USRP::kUSRPUnknown)
        .value("Simulated", USRP::kSimulated)
        .export_values();

    py::implicitly_convertible<py::str, USRP::DeviceType>();

    py::class_<USRP, std::shared_ptr<USRP>>(m, "USRP")
        // USRP(...) still constructs the UHD hardware implementation.
        .def(py::init([](const std::string& addr,
                         const std::optional<std::string>& tx_subdev,
                         const std::optional<std::string>& rx_subdev,
                         double freq,
                         const std::string& tx_ant,
                         const std::string& rx_ant,
                         float tx_gain,
                         float rx_gain) -> std::shared_ptr<USRP>
            {
                return std::make_shared<UHDUSRP>(addr, tx_subdev, rx_subdev, freq, tx_ant, rx_ant, tx_gain, rx_gain);
            }))
        .def_property_readonly("device_type",
            &USRP::getDeviceType)
        .def_property_readonly("clock_rate",
            &USRP::getMasterClockRate)
        .def_property("tx_frequency",
            &USRP::getTXFrequency,
            &USRP::setTXFrequency)
        .def_property("rx_frequency",
            &USRP::getRXFrequency,
            &USRP::setRXFrequency)
        .def_property("tx_rate",
            &USRP::getTXRate,
            &USRP::setTXRate)
        .def_property("rx_rate",
            &USRP::getRXRate,
            &USRP::setRXRate)
        .def_property("tx_gain",
            &USRP::getTXGain,
            &USRP::setTXGain)
        .def_property("rx_gain",
            &USRP::getRXGain,
            &USRP::setRXGain)
        .def_property_readonly("clock_sources",
            [](std::shared_ptr<USRP> self)
            {
                return self->getClockSources();
            })
        .def_property("clock_source",
            [](std::shared_ptr<USRP> self)
            {
                return self->getClockSource();
            },
            [](std::shared_ptr<USRP> self, const std::string &clock_src)
            {
                return self->setClockSource(clock_src);
            })
        .def_property_readonly("time_sources",
            [](std::shared_ptr<USRP> self)
            {
                return self->getTimeSources();
            })
        .def_property("time_source",
            [](std::shared_ptr<USRP> self)
            {
                return self->getTimeSource();
            },
            [](std::shared_ptr<USRP> self, const std::string &time_src)
            {
                return self->setTimeSource(time_src);
            })
        .def_property("tx_max_samps",
            &USRP::getMaxTXSamps,
            &USRP::setMaxTXSamps)
        .def_property("tx_max_samps_factor",
            nullptr,
            &USRP::setMaxTXSampsFactor)
        .def_property("rx_max_samps",
            &USRP::getMaxRXSamps,
            &USRP::setMaxRXSamps)
        .def_property("rx_max_samps_factor",
            nullptr,
            &USRP::setMaxRXSampsFactor)
        .def_property("auto_dc_offset",
            &USRP::getAutoDCOffset,
            &USRP::setAutoDCOffset)
        .def("getClockSource",
            &USRP::getClockSource)
        .def("setClockSource",
            &USRP::setClockSource)
        .def("getTimeSource",
            &USRP::getTimeSource)
        .def("setTimeSource",
            &USRP::setTimeSource)
        .def("syncTimeToPPS",
            &USRP::syncTimeToPPS,
            "Align device time to the PPS input (time source external); returns the edge's offset from the host's whole second",
            py::arg("timeout") = 3.0,
            py::call_guard<py::gil_scoped_release>())
        .def("stop",
            &USRP::stop)
        .def_property_readonly("tx_underflow_count",
            &USRP::getTXUnderflowCount)
        .def_property_readonly("tx_late_count",
            &USRP::getTXLateCount)
        ;

    m.def("checkReference",
        &checkReferenceUHD,
        "Read-only reference check: lock state and PPS edges counted over `seconds` (nothing is transmitted)",
        py::arg("addr"),
        py::arg("clock_source") = "external",
        py::arg("time_source") = "external",
        py::arg("seconds") = 4.0,
        py::call_guard<py::gil_scoped_release>());
}
