#include "phy/Channels.hh"
#include "phy/ParallelPacketModulator.hh"
#include "phy/ParallelPacketDemodulator.hh"
#include "python/PyModules.hh"

void exportPacketModulators(py::module &m)
{
    // Export class PacketModulator to Python
    py::class_<PacketModulator, std::shared_ptr<PacketModulator>>(m, "PacketModulator")
        .def_property("tx_rate",
            &PacketModulator::getTXRate,
            &PacketModulator::setTXRate)
        .def_property("channel_rate",
            &PacketModulator::getChannelRate,
            &PacketModulator::setChannelRate)
        .def_property("channels",
            &PacketModulator::getChannels,
            &PacketModulator::setChannels)
        .def_property("tx_channel",
            &PacketModulator::getTXChannel,
            &PacketModulator::setTXChannel)
        ;

    // Export class PacketDemodulator to Python
    py::class_<PacketDemodulator, std::shared_ptr<PacketDemodulator>>(m, "PacketDemodulator")
        .def_property("rx_rate",
            &PacketDemodulator::getRXRate,
            &PacketDemodulator::setRXRate)
        .def_property("channel_rate",
            &PacketDemodulator::getChannelRate,
            &PacketDemodulator::setChannelRate)
        .def_property("channels",
            &PacketDemodulator::getChannels,
            &PacketDemodulator::setChannels)
        .def("setWindowParameters",
            &PacketDemodulator::setWindowParameters)
        ;

    // Export class ParallelPacketModulator to Python
    py::class_<ParallelPacketModulator, PacketModulator, std::shared_ptr<ParallelPacketModulator>>(m, "ParallelPacketModulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      unsigned int>())
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
