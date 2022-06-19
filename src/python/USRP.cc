// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/stl.h>

#include "USRP.hh"
#include "python/PyModules.hh"

using namespace pybind11::literals;

void exportUSRP(py::module &m)
{
    py::class_<USRP, Radio, MonoClock::TimeKeeper, std::shared_ptr<USRP>>(m, "USRP")
        .def(py::init<const std::string&>(),
            "USRP device address"_a)
        .def_property_readonly("mboard",
            &USRP::getMboard)
        .def_property("auto_dc_offset",
            &USRP::getAutoDCOffset,
            &USRP::setAutoDCOffset)
        // TX and RX samples
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
        // Antennas
        .def_property_readonly("tx_antennas",
            [](std::shared_ptr<USRP> self)
            {
                return self->getTXAntennas();
            })
        .def_property("tx_antenna",
            [](std::shared_ptr<USRP> self)
            {
                return self->getTXAntenna();
            },
            [](std::shared_ptr<USRP> self, const std::string &ant)
            {
                return self->setTXAntenna(ant);
            })
        .def_property_readonly("rx_antennas",
            [](std::shared_ptr<USRP> self)
            {
                return self->getRXAntennas();
            })
        .def_property("rx_antenna",
            [](std::shared_ptr<USRP> self)
            {
                return self->getRXAntenna();
            },
            [](std::shared_ptr<USRP> self, const std::string &ant)
            {
                return self->setRXAntenna(ant);
            })
        .def("getTXAntennas",
            &USRP::getTXAntennas,
            "channel index"_a=0)
        .def("getTXAntenna",
            &USRP::getTXAntenna,
            "channel index"_a=0)
        .def("setTXAntenna",
            &USRP::setTXAntenna,
            "antenna"_a,
            "channel index"_a=0)
        .def("getRXAntennas",
            &USRP::getRXAntennas,
            "channel index"_a=0)
        .def("getRXAntenna",
            &USRP::getRXAntenna,
            "channel index"_a=0)
        .def("setRXAntenna",
            &USRP::setRXAntenna,
            "antenna"_a,
            "channel index"_a=0)
        // Subdevices
        .def_property("tx_subdev_spec",
            [](std::shared_ptr<USRP> self)
            {
                return self->getTXSubdevSpec();
            },
            [](std::shared_ptr<USRP> self, const std::string &spec)
            {
                return self->setTXSubdevSpec(spec);
            })
        .def_property("rx_subdev_spec",
            [](std::shared_ptr<USRP> self)
            {
                return self->getRXSubdevSpec();
            },
            [](std::shared_ptr<USRP> self, const std::string &spec)
            {
                return self->setRXSubdevSpec(spec);
            })
        .def("getTXSubdevSpec",
            &USRP::getTXSubdevSpec,
            "channel index"_a=0)
        .def("setTXSubdevSpec",
            &USRP::setTXSubdevSpec,
            "frontend specification"_a,
            "channel index"_a=0)
        .def("getRXSubdevSpec",
            &USRP::getRXSubdevSpec,
            "channel index"_a=0)
        .def("setRXSubdevSpec",
            &USRP::setRXSubdevSpec,
            "frontend specification"_a,
            "channel index"_a=0)
        // Master clock rate
        .def_property("clock_rate",
            [](std::shared_ptr<USRP> self)
            {
                return self->getMasterClockRate();
            },
            [](std::shared_ptr<USRP> self, double rate)
            {
                return self->setMasterClockRate(rate);
            })
        .def("getMasterClockRate",
            [](std::shared_ptr<USRP> self, size_t mboard)
            {
                return self->getMasterClockRate(mboard);
            })
        .def("setMasterClockRate",
            &USRP::setMasterClockRate)
        // Clocks and time
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
        .def("getClockSources",
            &USRP::getClockSources)
        .def("getClockSource",
            &USRP::getClockSource)
        .def("setClockSource",
            &USRP::setClockSource)
        .def("getTimeSources",
            &USRP::getTimeSources)
        .def("getTimeSource",
            &USRP::getTimeSource)
        .def("setTimeSource",
            &USRP::setTimeSource)
        .def("syncTime",
            &USRP::syncTime,
            py::arg("random_bias")=false,
            py::arg("use_pps")=false)
        ;
}
