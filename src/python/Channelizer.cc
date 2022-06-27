// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/FDChannelizer.hh"
#include "phy/TDChannelizer.hh"
#include "python/PyModules.hh"

void exportChannelizers(py::module &m)
{
    // Export class Channelizer to Python
    py::class_<Channelizer, std::shared_ptr<Channelizer>>(m, "Channelizer")
        .def_property("rx_rate",
            &Channelizer::getRXRate,
            &Channelizer::setRXRate,
            "float: RX rate")
        .def_property("channels",
            &Channelizer::getChannels,
            &Channelizer::setChannels,
            "Sequence[Channel]: channels")
        .def_property_readonly("source",
            [](std::shared_ptr<Channelizer> e)
            {
                return exposePort(e, &e->source);
            })
        ;

    // Export class FDChannelizer to Python
    py::class_<FDChannelizer, Channelizer, std::shared_ptr<FDChannelizer>>(m, "FDChannelizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>())
        .def_readonly_static("P",
            &FDChannelizer::P,
            "int: Maximum prototype filter length.")
        .def_readonly_static("V",
            &FDChannelizer::V,
            "int: Overlap factor.")
        .def_readonly_static("N",
            &FDChannelizer::N,
            "int: FFT size.")
        .def_readonly_static("L",
            &FDChannelizer::L,
            "int: Samples consumer per input block.")
        ;

    // Export class TDChannelizer to Python
    py::class_<TDChannelizer, Channelizer, std::shared_ptr<TDChannelizer>>(m, "TDChannelizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("rx_rate"),
            py::arg("nthreads"))
        ;
}
