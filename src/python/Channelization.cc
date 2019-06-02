#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/FDChannelizer.hh"
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

    // Export class FDChannelizer to Python
    py::class_<FDChannelizer, Channelizer, std::shared_ptr<FDChannelizer>>(m, "FDChannelizer")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      double,
                      const Channels&,
                      unsigned int>())
        .def_property("taps",
            &FDChannelizer::getTaps,
            &FDChannelizer::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
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
        .def_property_readonly("source",
            [](std::shared_ptr<FDChannelizer> e)
            {
                return exposePort(e, &e->source);
            })
        ;

    // Export class TDChannelizer to Python
    py::class_<TDChannelizer, Channelizer, std::shared_ptr<TDChannelizer>>(m, "TDChannelizer")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      double,
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
                      double,
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
