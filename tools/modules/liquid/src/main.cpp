#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <liquid/liquid.h>

#include "FlexFrame.hh"
#include "OFDM.hh"
#include "PHY.hh"
#include "dsp/Resample.hh"
#include "python/PyModules.hh"

namespace py = pybind11;

PYBIND11_MODULE(liquid, m) {
    exportLiquidEnums(m);
    exportMCS(m);
    exportResamplers(m);
    exportNCOs(m);
    exportFilters(m);

    // Export class FrameStats to Python
    py::class_<FrameStats, std::shared_ptr<FrameStats>>(m, "FrameStats")
        .def(py::init<>())
        .def_readonly("evm", &FrameStats::evm, "Error Vector Magnitude (dB)")
        .def_readonly("rssi", &FrameStats::rssi, "Received Signal Strength Indicator (dB)")
        .def_readonly("cfo", &FrameStats::evm, "Carrier Frequency Offset (f/Fs)")
        .def_property_readonly("mod_scheme",
            [](const FrameStats &stats) { return static_cast<modulation_scheme>(stats.mod_scheme); },
            "Modulation scheme")
        .def_readonly("mod_bps", &FrameStats::mod_bps, "Modulation depth (bits/symbol)")
        .def_property_readonly("check",
            [](const FrameStats &stats) { return static_cast<crc_scheme>(stats.check); },
            "Data validity check (crc, checksum)")
        .def_property_readonly("fec0",
            [](const FrameStats &stats) { return static_cast<fec_scheme>(stats.fec0); },
            "Forward Error-Correction (inner)")
        .def_property_readonly("fec1",
            [](const FrameStats &stats) { return static_cast<fec_scheme>(stats.fec1); },
            "Forward Error-Correction (outer)")
        .def("__repr__", [](const FrameStats& self) {
            return py::str("FrameStats(evm={:0.2g}, rssi={:0.2g}, cfo={:0.2g}, mod_scheme={}, mod_bps={}, check={}, fec0={}, fec1={})").\
            format(self.evm, self.rssi, self.cfo,
                   static_cast<modulation_scheme>(self.mod_scheme),
                   self.mod_bps,
                   static_cast<crc_scheme>(self.check),
                   static_cast<fec_scheme>(self.fec0),
                   static_cast<fec_scheme>(self.fec1));
         })
        ;

    // Export class Header to Python
    py::class_<Header, std::shared_ptr<Header>>(m, "Header")
        .def(py::init<>())
        .def(py::init<uint8_t,
                      uint8_t,
                      uint16_t,
                      uint16_t,
                      uint16_t>())
        .def_readwrite("curhop", &Header::curhop, "Current hop")
        .def_readwrite("nexthop", &Header::nexthop, "Next hop")
        .def_readwrite("flags", &Header::flags, "Packet flags")
        .def_readwrite("seq", &Header::seq, "Packet sequence number")
        .def_readwrite("data_len", &Header::data_len, "Size of actual packet data")
        .def("__repr__", [](const Header& self) {
            return py::str("Header(curhop={}, nexthop={}, flags={:x}, seq={}, data_len={})").\
            format(self.curhop, self.nexthop, self.flags, self.seq, self.data_len);
         })
        ;

    // Export class Modulator to Python
    py::class_<Modulator, std::shared_ptr<Modulator>>(m, "Modulator")
        .def_property("header_mcs", &Modulator::getHeaderMCS, &Modulator::setHeaderMCS, "Header MCS")
        .def_property("payload_mcs", &Modulator::getPayloadMCS, &Modulator::setPayloadMCS, "Payload MCS")
        .def("modulate", &Modulator::modulate, "Modulate a packet")
        ;

    // Export class Demodulator to Python
    py::class_<Demodulator, std::shared_ptr<Demodulator>>(m, "Demodulator")
        .def_property("header_mcs", &Demodulator::getHeaderMCS, &Demodulator::setHeaderMCS, "Header MCS")
        .def_property_readonly("soft_header", &Demodulator::getSoftHeader, "Use soft decoding for header")
        .def_property_readonly("soft_payload", &Demodulator::getSoftPayload, "Use soft decoding for payload")
        .def("reset", &Demodulator::reset, "Reset demodulator state")
        .def("demodulate", &Demodulator::demodulate, "Demodulate a signal")
        ;

    // Export class OFDMModulator to Python
    py::class_<OFDMModulator, Modulator, std::shared_ptr<OFDMModulator>>(m, "OFDMModulator")
        .def(py::init<unsigned,
                      unsigned,
                      unsigned>())
        ;

    // Export class OFDMDemodulator to Python
    py::class_<OFDMDemodulator, Demodulator, std::shared_ptr<OFDMDemodulator>>(m, "OFDMDemodulator")
        .def(py::init<bool,
                      bool,
                      unsigned,
                      unsigned,
                      unsigned>())
        ;

    // Export class FlexFrameModulator to Python
    py::class_<FlexFrameModulator, Modulator, std::shared_ptr<FlexFrameModulator>>(m, "FlexFrameModulator")
        .def(py::init<>())
        ;

    // Export class FlexFrameDemodulator to Python
    py::class_<FlexFrameDemodulator, Demodulator, std::shared_ptr<FlexFrameDemodulator>>(m, "FlexFrameDemodulator")
        .def(py::init<bool,
                      bool>())
        ;

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}
