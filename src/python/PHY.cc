#include "phy/FlexFrame.hh"
#include "phy/Gain.hh"
#include "phy/MultiOFDM.hh"
#include "phy/NewFlexFrame.hh"
#include "phy/OFDM.hh"
#include "python/PyModules.hh"

using Liquid::ResamplerParams;

void exportPHYs(py::module &m)
{
    // Export class Gain to Python
    py::class_<Gain, std::shared_ptr<Gain>>(m, "Gain")
        .def_property("lin", &Gain::getLinearGain, &Gain::setLinearGain)
        .def_property("dB", &Gain::getDbGain, &Gain::setDbGain)
        .def("__repr__", [](const Gain& self) {
            return py::str("Gain(lin={}, dB={})").format(self.getLinearGain(), self.getDbGain());
         })
        ;

    // Export class PHY to Python
    py::class_<PHY, std::shared_ptr<PHY>>(m, "PHY")
        .def_property_readonly("min_rx_rate_oversample",
            &PHY::getMinRXRateOversample)
        .def_property_readonly("min_tx_rate_oversample",
            &PHY::getMinTXRateOversample)
        .def_property("rx_rate",
            &PHY::getRXRate,
            &PHY::setRXRate)
        .def_property("tx_rate",
            &PHY::getTXRate,
            &PHY::setTXRate)
        .def_property("channel_rate",
            &PHY::getChannelRate,
            &PHY::setChannelRate)
        ;

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

    // Export class LiquidPHY to Python
    py::class_<LiquidPHY, PHY, std::shared_ptr<LiquidPHY>>(m, "LiquidPHY")
        .def_property_readonly("header_mcs",
            &LiquidPHY::getHeaderMCS)
        .def_property_readonly("soft_header",
            &LiquidPHY::getSoftHeader)
        .def_property_readonly("soft_payload",
            &LiquidPHY::getSoftPayload)
        .def_property("min_packet_size",
            &LiquidPHY::getMinPacketSize,
            &LiquidPHY::setMinPacketSize)
        .def_readwrite("upsamp_resamp_params",
            &LiquidPHY::upsamp_resamp_params,
            py::return_value_policy::reference_internal)
        .def_readwrite("downsamp_resamp_params",
            &LiquidPHY::downsamp_resamp_params,
            py::return_value_policy::reference_internal)
        ;

    // Export class FlexFrame to Python
    py::class_<FlexFrame, LiquidPHY, std::shared_ptr<FlexFrame>>(m, "FlexFrame")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const MCS&,
                      bool,
                      bool,
                      size_t>())
        ;

    // Export class NewFlexFrame to Python
    py::class_<NewFlexFrame, LiquidPHY, std::shared_ptr<NewFlexFrame>>(m, "NewFlexFrame")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const MCS&,
                      bool,
                      bool,
                      size_t>())
        ;

    // Export class OFDM to Python
    py::class_<OFDM, LiquidPHY, std::shared_ptr<OFDM>>(m, "OFDM")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const MCS&,
                      bool,
                      bool,
                      size_t,
                      unsigned int,
                      unsigned int,
                      unsigned int>())
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const MCS&,
                      bool,
                      bool,
                      size_t,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      std::vector<unsigned char>&>())
        ;

    // Export class MultiOFDM to Python
    py::class_<MultiOFDM, LiquidPHY, std::shared_ptr<MultiOFDM>>(m, "MultiOFDM")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const MCS&,
                      bool,
                      bool,
                      size_t,
                      unsigned int,
                      unsigned int,
                      unsigned int>())
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const MCS&,
                      bool,
                      bool,
                      size_t,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      std::vector<unsigned char>&>())
        ;
}
