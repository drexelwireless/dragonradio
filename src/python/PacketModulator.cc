#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "phy/Channel.hh"
#include "phy/ParallelPacketModulator.hh"
#include "phy/ParallelPacketDemodulator.hh"
#include "phy/PerChannelDemodulator.hh"
#include "python/PyModules.hh"

void exportPacketModulators(py::module &m)
{
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

    // Export class PerChannelDemodulator to Python
    py::class_<PerChannelDemodulator, PacketDemodulator, std::shared_ptr<PerChannelDemodulator>>(m, "PerChannelDemodulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      unsigned int>())
        .def_property("taps",
            &PerChannelDemodulator::getTaps,
            &PerChannelDemodulator::setTaps,
            "Prototype filter for channelization. Should have unity gain.")
        .def_property_readonly("source",
            [](std::shared_ptr<PerChannelDemodulator> e)
            {
                return exposePort(e, &e->source);
            })
        ;
}
