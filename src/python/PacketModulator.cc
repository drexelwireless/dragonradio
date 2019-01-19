#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/ParallelPacketModulator.hh"
#include "phy/ParallelPacketDemodulator.hh"
#include "python/PyModules.hh"

using Liquid::ResamplerParams;

void exportPacketModulators(py::module &m)
{
    // Export class ResamplerParams to Python
    py::class_<ResamplerParams, std::shared_ptr<ResamplerParams>>(m, "ResamplerParams")
        .def_property("m",
            &ResamplerParams::get_m,
            &ResamplerParams::set_m)
        .def_property("fc",
            &ResamplerParams::get_fc,
            &ResamplerParams::set_fc)
        .def_property("As",
            &ResamplerParams::get_As,
            &ResamplerParams::set_As)
        .def_property("npfb",
            &ResamplerParams::get_npfb,
            &ResamplerParams::set_npfb)
        .def("__repr__", [](const ResamplerParams& self) {
            return py::str("ResamplerParams(m={}, fc={}, As={}, npfb={})").format(self.m, self.fc, self.As, self.npfb);
         })
        ;

    // Export class PacketModulator to Python
    py::class_<PacketModulator, std::shared_ptr<PacketModulator>>(m, "PacketModulator")
        .def_property("tx_rate",
            &PacketModulator::getTXRate,
            &PacketModulator::setTXRate)
        ;

    // Export class PacketDemodulator to Python
    py::class_<PacketDemodulator, std::shared_ptr<PacketDemodulator>>(m, "PacketDemodulator")
        .def_property("rx_rate",
            &PacketDemodulator::getRXRate,
            &PacketDemodulator::setRXRate)
        .def_property("channels",
            &PacketDemodulator::getChannels,
            &PacketDemodulator::setChannels)
        ;

    // Export class ParallelPacketModulator to Python
    py::class_<ParallelPacketModulator, PacketModulator, std::shared_ptr<ParallelPacketModulator>>(m, "ParallelPacketModulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channel&,
                      unsigned int>())
        .def_property("taps",
            &ParallelPacketModulator::getTaps,
            &ParallelPacketModulator::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property("tx_channel",
            &ParallelPacketModulator::getTXChannel,
            &ParallelPacketModulator::setTXChannel)
        .def_property_readonly("sink",
            [](std::shared_ptr<ParallelPacketModulator> element)
            {
                return exposePort(element, &element->sink);
            })
        ;

    // Export class ParallelPacketDemodulator to Python
    py::class_<ParallelPacketDemodulator, PacketDemodulator, std::shared_ptr<ParallelPacketDemodulator>>(m, "ParallelPacketDemodulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      unsigned int>())
        .def_property("taps",
            &ParallelPacketDemodulator::getTaps,
            &ParallelPacketDemodulator::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property("prev_demod",
            &ParallelPacketDemodulator::getPrevDemod,
            &ParallelPacketDemodulator::setPrevDemod)
        .def_property("cur_demod",
            &ParallelPacketDemodulator::getCurDemod,
            &ParallelPacketDemodulator::setCurDemod)
        .def_property("enforce_ordering",
            &ParallelPacketDemodulator::getEnforceOrdering,
            &ParallelPacketDemodulator::setEnforceOrdering)
        .def_property_readonly("source",
            [](std::shared_ptr<ParallelPacketDemodulator> e)
            {
                return exposePort(e, &e->source);
            })
        ;
}
