// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "logging.hh"
#include "phy/Channel.hh"
#include "phy/FDChannelModulator.hh"
#include "phy/MultichannelSynthesizer.hh"
#include "phy/ParallelChannelSynthesizer.inl"
#include "phy/SlotSynthesizer.hh"
#include "phy/Synthesizer.hh"
#include "phy/TDChannelModulator.hh"
#include "phy/UnichannelSynthesizer.inl"
#include "python/PyModules.hh"

void exportSynthesizers(py::module &m)
{
    // Export class Synthesizer to Python
    py::class_<Synthesizer, std::shared_ptr<Synthesizer>>(m, "Synthesizer")
        .def_property("tx_rate",
            &Synthesizer::getTXRate,
            &Synthesizer::setTXRate,
            "float: TX rate")
        .def_property("channels",
            &Synthesizer::getChannels,
            &Synthesizer::setChannels,
            "Sequence[Channel]: TX channels")
        .def_property("schedule",
            &SlotSynthesizer::getSchedule,
            py::overload_cast<const Schedule::sched_type &>(&SlotSynthesizer::setSchedule),
            "Schedule: MAC schedule")
        .def_property_readonly("sink",
            [](std::shared_ptr<Synthesizer> element)
            {
                return exposePort(element, &element->sink);
            })
        ;

    // Export class ChannelSynthesizer to Python
    py::class_<ChannelSynthesizer, Synthesizer, std::shared_ptr<ChannelSynthesizer>>(m, "ChannelSynthesizer")
        .def_property("high_water_mark",
            &ChannelSynthesizer::getHighWaterMark,
            &ChannelSynthesizer::setHighWaterMark,
            "int: Maximum number of modulated samples to queue.")
        ;
        ;

    // Export class TDSynthesizer to Python
    using TDSynthesizer = ParallelChannelSynthesizer<TDChannelModulator>;

    py::class_<TDSynthesizer, ChannelSynthesizer, std::shared_ptr<TDSynthesizer>>(m, "TDSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        ;

    // Export class FDSynthesizer to Python
    using FDSynthesizer = ParallelChannelSynthesizer<FDChannelModulator>;

    py::class_<FDSynthesizer, ChannelSynthesizer, std::shared_ptr<FDSynthesizer>>(m, "FDSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        ;

    // Export class SlotSynthesizer to Python
    py::class_<SlotSynthesizer, Synthesizer, std::shared_ptr<SlotSynthesizer>>(m, "SlotSynthesizer")
        .def_property("superslots",
            &SlotSynthesizer::getSuperslots,
            &SlotSynthesizer::setSuperslots,
            "bool: Flag indicating whether or not to use superslots.")
        ;

    // Export class TDSlotSynthesizer to Python
    using TDSlotSynthesizer = UnichannelSynthesizer<TDChannelModulator>;

    py::class_<TDSlotSynthesizer, SlotSynthesizer, std::shared_ptr<TDSlotSynthesizer>>(m, "TDSlotSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        ;

    // Export class FDSlotSynthesizer to Python
    using FDSlotSynthesizer = UnichannelSynthesizer<FDChannelModulator>;

    py::class_<FDSlotSynthesizer, SlotSynthesizer, std::shared_ptr<FDSlotSynthesizer>>(m, "FDSlotSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        ;

    // Export class MultichannelSynthesizer to Python
    py::class_<MultichannelSynthesizer, SlotSynthesizer, std::shared_ptr<MultichannelSynthesizer>>(m, "MultichannelSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        ;
}
