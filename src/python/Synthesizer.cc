#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/FDChannelModulator.hh"
#include "phy/MultichannelSynthesizer.hh"
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
            &Synthesizer::setTXRate)
        .def_property("channels",
            &Synthesizer::getChannels,
            &Synthesizer::setChannels,
            "TX channels")
        .def_property("schedule",
            &SlotSynthesizer::getSchedule,
            py::overload_cast<const Schedule::sched_type &>(&SlotSynthesizer::setSchedule),
            "MAC schedule")
        .def_property_readonly("sink",
            [](std::shared_ptr<Synthesizer> element)
            {
                return exposePort(element, &element->sink);
            })
        ;

    // Export class SlotSynthesizer to Python
    py::class_<SlotSynthesizer, Synthesizer, std::shared_ptr<SlotSynthesizer>>(m, "SlotSynthesizer")
        .def_property("superslots",
            &SlotSynthesizer::getSuperslots,
            &SlotSynthesizer::setSuperslots,
            "Flag indicating whether or not to use superslots.")
        ;

    // Export class TDSlotSynthesizer to Python
    using TDSlotSynthesizer = UnichannelSynthesizer<TDChannelModulator>;

    py::class_<TDSlotSynthesizer, SlotSynthesizer, std::shared_ptr<TDSlotSynthesizer>>(m, "TDSlotSynthesizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        ;

    // Export class FDSlotSynthesizer to Python
    using FDSlotSynthesizer = UnichannelSynthesizer<FDChannelModulator>;

    py::class_<FDSlotSynthesizer, SlotSynthesizer, std::shared_ptr<FDSlotSynthesizer>>(m, "FDSlotSynthesizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        ;

    // Export class MultichannelSynthesizer to Python
    py::class_<MultichannelSynthesizer, SlotSynthesizer, std::shared_ptr<MultichannelSynthesizer>>(m, "MultichannelSynthesizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        ;
}
