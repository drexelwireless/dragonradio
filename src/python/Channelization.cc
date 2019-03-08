#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/OverlapTDChannelizer.hh"
#include "phy/Synthesizer.hh"
#include "phy/TDChannelizer.hh"
#include "phy/TDSynthesizer.hh"
#include "python/PyModules.hh"

void exportPacketModulators(py::module &m)
{
    // Export class Channelizer to Python
    py::class_<Channelizer, std::shared_ptr<Channelizer>>(m, "Channelizer")
        .def_property("rx_rate",
            &Channelizer::getRXRate,
            &Channelizer::setRXRate)
        .def_property("channels",
            &Channelizer::getChannels,
            &Channelizer::setChannels)
        ;

    // Export class TDChannelizer to Python
    py::class_<TDChannelizer, Channelizer, std::shared_ptr<TDChannelizer>>(m, "TDChannelizer")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      unsigned int>())
        .def_property("taps",
            &TDChannelizer::getTaps,
            &TDChannelizer::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property_readonly("source",
            [](std::shared_ptr<TDChannelizer> e)
            {
                return exposePort(e, &e->source);
            })
        ;

    // Export class OverlapTDChannelizer to Python
    py::class_<OverlapTDChannelizer, Channelizer, std::shared_ptr<OverlapTDChannelizer>>(m, "OverlapTDChannelizer")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      unsigned int>())
        .def_property("taps",
            &OverlapTDChannelizer::getTaps,
            &OverlapTDChannelizer::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property("prev_demod",
            &OverlapTDChannelizer::getPrevDemod,
            &OverlapTDChannelizer::setPrevDemod)
        .def_property("cur_demod",
            &OverlapTDChannelizer::getCurDemod,
            &OverlapTDChannelizer::setCurDemod)
        .def_property("enforce_ordering",
            &OverlapTDChannelizer::getEnforceOrdering,
            &OverlapTDChannelizer::setEnforceOrdering)
        .def_property_readonly("source",
            [](std::shared_ptr<OverlapTDChannelizer> e)
            {
                return exposePort(e, &e->source);
            })
        ;

    // Export class Synthesizer to Python
    py::class_<Synthesizer, std::shared_ptr<Synthesizer>>(m, "Synthesizer")
        .def_property("tx_rate",
            &Synthesizer::getTXRate,
            &Synthesizer::setTXRate)
        ;

    // Export class TDSynthesizer to Python
    py::class_<TDSynthesizer, Synthesizer, std::shared_ptr<TDSynthesizer>>(m, "TDSynthesizer")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channel&,
                      unsigned int>())
        .def_property("taps",
            &TDSynthesizer::getTaps,
            &TDSynthesizer::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property("tx_channel",
            &TDSynthesizer::getTXChannel,
            &TDSynthesizer::setTXChannel)
        .def_property_readonly("sink",
            [](std::shared_ptr<TDSynthesizer> element)
            {
                return exposePort(element, &element->sink);
            })
        ;
}
