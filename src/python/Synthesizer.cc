#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/Synthesizer.hh"
#include "phy/TDSynthesizer.hh"
#include "python/PyModules.hh"

void exportSynthesizers(py::module &m)
{
    // Export class Synthesizer to Python
    py::class_<Synthesizer, std::shared_ptr<Synthesizer>>(m, "Synthesizer")
        .def_property("tx_rate",
            &Synthesizer::getTXRate,
            &Synthesizer::setTXRate)
        .def_property("superslots",
            &Synthesizer::getSuperslots,
            &Synthesizer::setSuperslots,
            "Flag indicating whether or not to use superslots.")
        .def_property("channels",
            &Synthesizer::getChannels,
            &Synthesizer::setChannels,
            "TX channels")
        .def_property("schedule",
            &Synthesizer::getSchedule,
            py::overload_cast<const Schedule::sched_type &>(&Synthesizer::setSchedule),
            "MAC schedule")
        .def_property_readonly("sink",
            [](std::shared_ptr<Synthesizer> element)
            {
                return exposePort(element, &element->sink);
            })
        ;

    // Export class TDSynthesizer to Python
    py::class_<TDSynthesizer, Synthesizer, std::shared_ptr<TDSynthesizer>>(m, "TDSynthesizer")
        .def(py::init<std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        ;
}
