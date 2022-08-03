// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "logging.hh"
#include "phy/Channel.hh"
#include "phy/ChannelSynthesizer.inl"
#include "phy/FDChannelModulator.hh"
#include "phy/MultichannelSynthesizer.hh"
#include "phy/Synthesizer.hh"
#include "phy/TDChannelModulator.hh"
#include "python/PyModules.hh"

void exportSynthesizers(py::module &m)
{
    // Export class Synthesizer to Python
    py::class_<Synthesizer, std::shared_ptr<Synthesizer>>(m, "Synthesizer")
        .def_property("high_water_mark",
            &Synthesizer::getHighWaterMark,
            &Synthesizer::setHighWaterMark,
            "int: Maximum number of modulated samples to queue.")
        .def_property("tx_rate",
            &Synthesizer::getTXRate,
            &Synthesizer::setTXRate,
            "float: TX rate")
        .def_property("channels",
            &Synthesizer::getChannels,
            &Synthesizer::setChannels,
            "Sequence[Channel]: TX channels")
        .def_property("schedule",
            &Synthesizer::getSchedule,
            [](Synthesizer &self, py::object obj)
            {
                try {
                    return self.setSchedule(obj.cast<const Schedule::sched_type>());
                } catch (py::cast_error &) {
                    return self.setSchedule(obj.cast<const Schedule>());
                }
            },
            "Schedule: MAC schedule")
        .def_property_readonly("sink",
            [](std::shared_ptr<Synthesizer> element)
            {
                return exposePort(element, &element->sink);
            })
        ;

    // Export class TDSynthesizer to Python
    using TDSynthesizer = ChannelSynthesizer<TDChannelModulator>;

    py::class_<TDSynthesizer, Synthesizer, std::shared_ptr<TDSynthesizer>>(m, "TDSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        ;

    // Export class FDSynthesizer to Python
    using FDSynthesizer = ChannelSynthesizer<FDChannelModulator>;

    py::class_<FDSynthesizer, Synthesizer, std::shared_ptr<FDSynthesizer>>(m, "FDSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        .def_readonly_static("P",
            &FDChannelModulator::P,
            "int: Maximum prototype filter length.")
        ;

    // Export class MultichannelSynthesizer to Python
    py::class_<MultichannelSynthesizer, Synthesizer, std::shared_ptr<MultichannelSynthesizer>>(m, "MultichannelSynthesizer")
        .def(py::init<const std::vector<PHYChannel>&,
                      double,
                      unsigned int>(),
            py::arg("channels"),
            py::arg("tx_rate"),
            py::arg("nthreads"))
        .def_readonly_static("P",
            &MultichannelSynthesizer::P,
            "int: Maximum prototype filter length.")
        ;
}
