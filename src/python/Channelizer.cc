// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/FDChannelizer.hh"
#include "phy/OverlapTDChannelizer.hh"
#include "phy/TDChannelizer.hh"
#include "python/PyModules.hh"

void exportChannelizers(py::module &m)
{
    // Export class Channelizer to Python
    py::class_<Channelizer, std::shared_ptr<Channelizer>>(m, "Channelizer")
        .def_property("rx_rate",
            &Channelizer::getRXRate,
            &Channelizer::setRXRate)
        .def_property("channels",
            &Channelizer::getChannels,
            &Channelizer::setChannels)
        .def_property_readonly("source",
            [](std::shared_ptr<Channelizer> e)
            {
                return exposePort(e, &e->source);
            })
        ;

    // Export class FDChannelizer to Python
    py::class_<FDChannelizer, Channelizer, std::shared_ptr<FDChannelizer>>(m, "FDChannelizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        .def_readonly_static("P",
            &FDChannelizer::P,
            "Maximum prototype filter length.")
        .def_readonly_static("V",
            &FDChannelizer::V,
            "Overlap factor.")
        .def_readonly_static("N",
            &FDChannelizer::N,
            "FFT size.")
        .def_readonly_static("L",
            &FDChannelizer::L,
            "Samples consumer per input block.")
        ;

    // Export class TDChannelizer to Python
    py::class_<TDChannelizer, Channelizer, std::shared_ptr<TDChannelizer>>(m, "TDChannelizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        ;

    // Export class OverlapTDChannelizer to Python
    py::class_<OverlapTDChannelizer, Channelizer, std::shared_ptr<OverlapTDChannelizer>>(m, "OverlapTDChannelizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        .def_property("prev_demod",
            &OverlapTDChannelizer::getPrevDemod,
            &OverlapTDChannelizer::setPrevDemod)
        .def_property("cur_demod",
            &OverlapTDChannelizer::getCurDemod,
            &OverlapTDChannelizer::setCurDemod)
        .def_property("enforce_ordering",
            &OverlapTDChannelizer::getEnforceOrdering,
            &OverlapTDChannelizer::setEnforceOrdering)
        ;
}
