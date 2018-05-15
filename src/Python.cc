#include <pybind11/embed.h>

namespace py = pybind11;

#include "Logger.hh"
#include "RadioConfig.hh"
#include "USRP.hh"
#include "phy/FlexFrame.hh"
#include "phy/MultiOFDM.hh"
#include "phy/OFDM.hh"
#include "phy/ParallelPacketModulator.hh"
#include "phy/ParallelPacketDemodulator.hh"
#include "mac/SlottedMAC.hh"
#include "mac/TDMA.hh"
#include "net/Net.hh"

std::shared_ptr<Logger> mkLogger(const std::string& path)
{
    Clock::time_point t_start = Clock::time_point(Clock::now().get_full_secs());
    auto              log = std::make_shared<Logger>(t_start);

    log->open(path);
    log->setAttribute("start", (uint32_t) t_start.get_full_secs());

    return log;
}

PYBIND11_EMBEDDED_MODULE(dragonradio, m) {
    // Create enum type CRCScheme for liquid CRC schemes
    py::enum_<crc_scheme> crc(m, "CRCScheme");

    crc.def(py::init([](std::string value) -> crc_scheme {
            auto crc = liquid_getopt_str2crc(value.c_str());
            if (crc == LIQUID_CRC_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type CRCScheme");
            return crc;
        }));

    for (unsigned int i = 0; i < LIQUID_CRC_NUM_SCHEMES; ++i)
        crc.value(crc_scheme_str[i][0], static_cast<crc_scheme>(i));

    crc.export_values();

    // Create enum type FECScheme for liquid FEC schemes
    py::enum_<fec_scheme> fec(m, "FECScheme");

    fec.def(py::init([](std::string value) -> fec_scheme {
            auto fec = liquid_getopt_str2fec(value.c_str());
            if (fec == LIQUID_FEC_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type FECScheme");
            return fec;
        }));

    for (unsigned int i = 0; i < LIQUID_FEC_NUM_SCHEMES; ++i)
        fec.value(fec_scheme_str[i][0], static_cast<fec_scheme>(i));

    fec.export_values();

    // Create enum type ModulationScheme for liquid modulation schemes
    py::enum_<modulation_scheme> ms(m, "ModulationScheme");

    ms.def(py::init([](std::string value) -> modulation_scheme {
           auto ms = liquid_getopt_str2mod(value.c_str());
           if (ms == LIQUID_MODEM_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type ModulationScheme");
           return ms;
       }));

    for (unsigned int i = 0; i < LIQUID_MODEM_NUM_SCHEMES; ++i)
        ms.value(modulation_types[i].name, static_cast<modulation_scheme>(i));

    ms.export_values();

    // Export class Logger to Python
    py::class_<Logger, std::shared_ptr<Logger>>(m, "Logger")
        .def_property_static("singleton",
            [](py::object) { return logger; },
            [](py::object, std::shared_ptr<Logger> log) { return logger = log; })
        .def(py::init(&mkLogger))
        .def("setAttribute", py::overload_cast<const std::string&, const std::string&>(&Logger::setAttribute))
        .def("setAttribute", py::overload_cast<const std::string&, uint8_t>(&Logger::setAttribute))
        .def("setAttribute", py::overload_cast<const std::string&, uint32_t>(&Logger::setAttribute))
        .def("setAttribute", py::overload_cast<const std::string&, double>(&Logger::setAttribute))
        ;

    // Export class RadioConfig to Python
    py::class_<RadioConfig, std::shared_ptr<RadioConfig>>(m, "RadioConfig")
        .def(py::init())
        .def_readwrite("verbose", &RadioConfig::verbose)
        .def_readwrite("soft_txgain", &RadioConfig::soft_txgain)
        .def_readwrite("ms", &RadioConfig::ms)
        .def_readwrite("check", &RadioConfig::check)
        .def_readwrite("fec0", &RadioConfig::fec0)
        .def_readwrite("fec1", &RadioConfig::fec1)
        ;

    // Export our global RadioConfig
    m.attr("rc") = rc;

    // Export class USRP to Python
    py::enum_<USRP::DeviceType>(m, "DeviceType")
        .value("N210", USRP::kUSRPN210)
        .value("X310", USRP::kUSRPX310)
        .value("Unknown", USRP::kUSRPUnknown)
        .export_values();

    py::class_<USRP, std::shared_ptr<USRP>>(m, "USRP")
        .def(py::init<const std::string&,
                      double,
                      const std::string&,
                      const std::string&,
                      float,
                      float>())
        .def_property_readonly("device_type", &USRP::getDeviceType)
        .def_property("tx_frequency", &USRP::getTXFrequency, &USRP::setTXFrequency)
        .def_property("rx_frequency", &USRP::getRXFrequency, &USRP::setRXFrequency)
        .def_property("tx_rate", &USRP::getTXRate, &USRP::setTXRate)
        .def_property("rx_rate", &USRP::getRXRate, &USRP::setRXRate)
        .def_property("tx_gain", &USRP::getTXGain, &USRP::setTXGain)
        .def_property("rx_gain", &USRP::getRXGain, &USRP::setRXGain)
        ;

    // Export class Node to Python
    py::class_<Node, std::shared_ptr<Node>>(m, "Node")
        .def_readonly("id", &Node::id, "Node ID")
        .def_readwrite("g", &Node::g, "Soft TX gain (multiplicative factor)")
        .def_readwrite("ms", &Node::ms, "Modulation scheme")
        .def_readwrite("check", &Node::check, "Data validity check")
        .def_readwrite("fec0", &Node::fec0, "Inner FEC")
        .def_readwrite("fec1", &Node::fec1, "Outer FEC")
        .def_property("soft_tx_gain", &Node::getSoftTXGain, &Node::setSoftTXGain, "Soft TX gain (dB)")
        .def_property("desired_soft_tx_gain", &Node::getDesiredSoftTXGain, &Node::setDesiredSoftTXGain, "Desired soft TX gain (dBFS)")
        .def_readwrite("desired_soft_tx_gain_clip_frac", &Node::desired_soft_tx_gain_clip_frac, "Clipping threshold for automatic TX soft gain")
        ;

    // Export class Net to Python
    py::class_<Net, std::shared_ptr<Net>>(m, "Net")
        .def(py::init<const std::string&,
                      const std::string&,
                      const std::string&,
                      size_t,
                      NodeId>())
        .def("__getitem__", [](Net &net, NodeId key) -> Node& {
            try {
                return net[key];
            } catch (const std::out_of_range&) {
                throw py::key_error("key '" + std::to_string(key) + "' does not exist");
            }
        }, py::return_value_policy::reference_internal)
        .def("__len__", &Net::size)
        .def("__iter__", [](Net &net) {
            return py::make_key_iterator(net.begin(), net.end());
         }, py::keep_alive<0, 1>())
        .def_property_readonly("my_node_id", &Net::getMyNodeId)
        .def("addNode", &Net::addNode)
        ;

    // Export class PHY to Python
    py::class_<PHY, std::shared_ptr<PHY>>(m, "PHY")
        .def("getRXRateOversample", &PHY::getRXRateOversample)
        .def("getTXRateOversample", &PHY::getTXRateOversample)
        ;

    // Export class FlexFrame to Python
    py::class_<FlexFrame, PHY, std::shared_ptr<FlexFrame>>(m, "FlexFrame")
        .def(py::init<std::shared_ptr<Net>,
                      size_t>())
        ;

    // Export class OFDM to Python
    py::class_<OFDM, PHY, std::shared_ptr<OFDM>>(m, "OFDM")
        .def(py::init<std::shared_ptr<Net>,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      size_t>())
        ;

    // Export class MultiOFDM to Python
    py::class_<MultiOFDM, PHY, std::shared_ptr<MultiOFDM>>(m, "MultiOFDM")
        .def(py::init<std::shared_ptr<Net>,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      size_t>())
        ;

    // Export class PacketModulator to Python
    py::class_<PacketModulator, std::shared_ptr<PacketModulator>>(m, "PacketModulator")
        .def_property("low_water_mark", &PacketModulator::getLowWaterMark, &PacketModulator::setLowWaterMark)
        ;

    // Export class ParallelPacketModulator to Python
    py::class_<ParallelPacketModulator, PacketModulator, std::shared_ptr<ParallelPacketModulator>>(m, "ParallelPacketModulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      unsigned int>())
        ;

    // Export class PacketDemodulator to Python
    py::class_<PacketDemodulator, std::shared_ptr<PacketDemodulator>>(m, "PacketDemodulator")
        ;

    // Export class ParallelPacketDemodulator to Python
    py::class_<ParallelPacketDemodulator, PacketDemodulator, std::shared_ptr<ParallelPacketDemodulator>>(m, "ParallelPacketDemodulator")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      unsigned int>())
        .def_property("ordered", &ParallelPacketDemodulator::getOrdered, &ParallelPacketDemodulator::setOrdered)
        ;

    // Export class MAC to Python
    py::class_<MAC, std::shared_ptr<MAC>>(m, "MAC")
        ;

    // Export class SlottedMAC to Python
    py::class_<SlottedMAC, MAC, std::shared_ptr<SlottedMAC>>(m, "SlottedMAC")
        .def_property("slot_size", &SlottedMAC::getSlotSize, &SlottedMAC::setSlotSize)
        .def_property("guard_size", &SlottedMAC::getGuardSize, &SlottedMAC::setGuardSize)
        ;

    // Export class TDMA to Python
    py::class_<TDMA, SlottedMAC, std::shared_ptr<TDMA>>(m, "TDMA")
        .def(py::init<std::shared_ptr<USRP>,
                      std::shared_ptr<PHY>,
                      std::shared_ptr<PacketModulator>,
                      std::shared_ptr<PacketDemodulator>,
                      double,
                      size_t,
                      double,
                      double>())
        .def("__getitem__", [](TDMA &mac, TDMA::slots_type::size_type i) {
            try {
                return mac[i];
            } catch (const std::out_of_range&) {
                throw py::index_error();
            }
        })
        .def("__setitem__", [](TDMA &mac, TDMA::slots_type::size_type i, bool v) {
            try {
                mac[i] = v;
            } catch (const std::out_of_range&) {
              throw py::index_error();
            }
        })
        .def("__len__", &TDMA::size)
        .def("__iter__", [](TDMA &mac) {
            return py::make_iterator(mac.begin(), mac.end());
         }, py::keep_alive<0, 1>())
        .def("resize", &TDMA::resize)
        ;
}
