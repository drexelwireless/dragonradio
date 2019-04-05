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
        .def_property_readonly("sink",
            [](std::shared_ptr<Synthesizer> element)
            {
                return exposePort(element, &element->sink);
            })
        ;

    // Export class TDSynthesizer to Python
    py::class_<TDSynthesizer, Synthesizer, std::shared_ptr<TDSynthesizer>>(m, "TDSynthesizer")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      double,
                      const Channel&,
                      unsigned int>())
        .def_property("taps",
            &TDSynthesizer::getTaps,
            &TDSynthesizer::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property("tx_channel",
            &TDSynthesizer::getTXChannel,
            &TDSynthesizer::setTXChannel)
        ;
}
