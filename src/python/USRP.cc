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
        .export_values();

    py::implicitly_convertible<py::str, USRP::DeviceType>();

    py::class_<USRP, Radio, MonoClock::TimeKeeper, std::shared_ptr<USRP>>(m, "USRP")
        .def(py::init<const std::string&,
                      const std::optional<std::string>&,
                      const std::optional<std::string>&,
                      double,
                      const std::string&,
                      const std::string&,
                      float,
                      float>())
        .def_property_readonly("device_type",
            &USRP::getDeviceType)
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
        .def("syncTime",
            &USRP::syncTime,
            py::arg("random_bias")=false)
        ;
}
