// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/stl.h>

#include "Radio.hh"
#include "python/PyModules.hh"

void exportRadio(py::module &m)
{
    // Export class Radio to Python
    py::class_<Radio, std::shared_ptr<Radio>>(m, "Radio")
        .def_property_readonly("clock_rate",
            &Radio::getMasterClockRate)
        .def_property("tx_frequency",
            &Radio::getTXFrequency,
            &Radio::setTXFrequency)
        .def_property("rx_frequency",
            &Radio::getRXFrequency,
            &Radio::setRXFrequency)
        .def_property("tx_rate",
            &Radio::getTXRate,
            &Radio::setTXRate)
        .def_property("rx_rate",
            &Radio::getRXRate,
            &Radio::setRXRate)
        .def_property("tx_gain",
            &Radio::getTXGain,
            &Radio::setTXGain)
        .def_property("rx_gain",
            &Radio::getRXGain,
            &Radio::setRXGain)
        ;
}
