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
        .def_readwrite("upsamp_params",
            &ParallelPacketModulator::upsamp_params,
            py::return_value_policy::reference_internal)
        ;

    // Export class ParallelPacketDemodulator to Python
    py::class_<ParallelPacketDemodulator, PacketDemodulator, std::shared_ptr<ParallelPacketDemodulator>>(m, "ParallelPacketDemodulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      unsigned int>())
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
        .def_readwrite("downsamp_params",
            &ParallelPacketDemodulator::downsamp_params,
            py::return_value_policy::reference_internal)
        ;
}
