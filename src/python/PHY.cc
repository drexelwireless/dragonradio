#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "phy/FlexFrame.hh"
#include "phy/Gain.hh"
#include "phy/NewFlexFrame.hh"
#include "phy/OFDM.hh"
#include "phy/PHY.hh"
#include "python/PyModules.hh"

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

    // Export class AutoGain to Python
    py::class_<AutoGain, std::shared_ptr<AutoGain>>(m, "AutoGain")
        .def(py::init<>())
        .def_property("g_0dBFS",
            &AutoGain::getSoftTXGain,
            &AutoGain::setSoftTXGain,
            "Soft TX gain (multiplicative factor)")
        .def_property("soft_tx_gain_0dBFS",
            &AutoGain::getSoftTXGain0dBFS,
            &AutoGain::setSoftTXGain0dBFS,
            "Soft TX gain (dBFS)")
        .def_property("auto_soft_tx_gain_clip_frac",
            &AutoGain::getAutoSoftTXGainClipFrac,
            &AutoGain::setAutoSoftTXGainClipFrac,
            "Clipping threshold for automatic TX soft gain")
        .def("recalc0dBFSEstimate",
            &AutoGain::recalc0dBFSEstimate,
            "Reset the 0dBFS estimate")
        ;

    // Export class PHY to Python
    py::class_<PHY, std::shared_ptr<PHY>>(m, "PHY")
        .def_readonly("mcs_table",
            &PHY::mcs_table)
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
        ;
}

void exportLiquidPHYs(py::module &m)
{
    // Export class Liquid::PHY to Python
    py::class_<Liquid::PHY, PHY, std::shared_ptr<Liquid::PHY>>(m, "LiquidPHY")
        .def_property_readonly("header_mcs",
            &Liquid::PHY::getHeaderMCS)
        .def_property_readonly("soft_header",
            &Liquid::PHY::getSoftHeader)
        .def_property_readonly("soft_payload",
            &Liquid::PHY::getSoftPayload)
        ;

    // Export class FlexFrame to Python
    py::class_<Liquid::FlexFrame, Liquid::PHY, std::shared_ptr<Liquid::FlexFrame>>(m, "FlexFrame")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const Liquid::MCS&,
                      const std::vector<std::pair<Liquid::MCS, AutoGain>>&,
                      bool,
                      bool>())
        ;

    // Export class NewFlexFrame to Python
    py::class_<Liquid::NewFlexFrame, Liquid::PHY, std::shared_ptr<Liquid::NewFlexFrame>>(m, "NewFlexFrame")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const Liquid::MCS&,
                      const std::vector<std::pair<Liquid::MCS, AutoGain>>&,
                      bool,
                      bool>())
        ;

    // Export class OFDM to Python
    py::class_<Liquid::OFDM, Liquid::PHY, std::shared_ptr<Liquid::OFDM>>(m, "OFDM")
        .def(py::init<std::shared_ptr<SnapshotCollector>,
                      NodeId,
                      const Liquid::MCS&,
                      const std::vector<std::pair<Liquid::MCS, AutoGain>>&,
                      bool,
                      bool,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      const std::optional<std::string>&>())
        .def_property_readonly("subcarriers",
            [](std::shared_ptr<Liquid::OFDM> self)
            {
                return static_cast<std::string>(self->getSubcarriers());
            })
        ;
}
