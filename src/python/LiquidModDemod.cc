#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "liquid/FlexFrame.hh"
#include "liquid/NewFlexFrame.hh"
#if defined(PYTHON_EXPORT_MULTIOFDM)
#include "liquid/MultiOFDM.hh"
#endif /* defined(PYTHON_EXPORT_MULTIOFDM) */
#include "liquid/OFDM.hh"
#include "python/PyModules.hh"

union PHYHeader {
    Header h;
    // FLEXFRAME_H_USER in liquid.internal.h. This is the largest header of any
    // of the liquid PHY implementations.
    unsigned char bytes[14];
};

// Initial modulation buffer size
const size_t kInitialModbufSize = 16384;

py::array_t<std::complex<float>> modulate(Liquid::Modulator &mod,
                                          const Header &hdr,
                                          py::buffer payload)
{
    PHYHeader header;
    auto buf = payload.request();

    memset(&header, 0, sizeof(header));
    header.h = hdr;

    mod.assemble(header.bytes, buf.ptr, buf.size);

    py::array_t<std::complex<float>> iqarr(kInitialModbufSize);
    auto                             iqbuf = iqarr.request();

    // Number of generated samples in the buffer
    size_t nsamples = 0;
    // Max number of samples generated by modulateSamples
    const size_t kMaxModSamples = mod.maxModulatedSamples();
    // Number of samples written
    size_t nw;
    // Flag indicating when we've reached the last symbol
    bool last_symbol = false;

    while (!last_symbol) {
        last_symbol = mod.modulateSamples(&static_cast<std::complex<float>*>(iqbuf.ptr)[nsamples], nw);
        nsamples += nw;

        // If we can't add another nw samples to the current IQ buffer, resize it.
        if (nsamples + kMaxModSamples > (size_t) iqbuf.size) {
            iqarr.resize({2*iqarr.size()});
            iqbuf = iqarr.request();
        }
    }

    // Resize the final buffer to the number of samples generated.
    iqarr.resize({nsamples});

    return iqarr;
}

using FrameStats = framesyncstats_s;

using demod_vec = std::vector<std::tuple<std::optional<Header>,
                              std::optional<py::bytes>,
                              FrameStats>>;

demod_vec demodulate(Liquid::Demodulator &demod,
                     py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> sig)
{
    demod_vec packets;

    auto buf = sig.request();

    Liquid::Demodulator::callback_t cb = [&](bool header_valid,
                                             const Header* header,
                                             bool payload_valid,
                                             void *payload,
                                             size_t payload_len,
                                             framesyncstats_s stats)
    {
        std::optional<Header>    h;
        std::optional<py::bytes> p;

        if (header_valid)
            h = *((Header *) header);

        if (payload_valid)
            p = py::bytes(reinterpret_cast<char*>(payload), payload_len);

        packets.push_back(std::make_tuple(h, p, stats));
    };

    demod.demodulate(static_cast<std::complex<float>*>(buf.ptr), buf.size, cb);

    return packets;
}

void exportLiquidModDemod(py::module &m)
{
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
            format(self.curhop, self.nexthop, self.flags, (unsigned) self.seq, self.data_len);
         })
        ;

    // Export class Liquid::Modulator to Python
    py::class_<Liquid::Modulator, std::shared_ptr<Liquid::Modulator>>(m, "LiquidModulator")
        .def_property("header_mcs",
            &Liquid::Modulator::getHeaderMCS,
            &Liquid::Modulator::setHeaderMCS,
            "Header MCS")
        .def_property("payload_mcs",
            &Liquid::Modulator::getPayloadMCS,
            &Liquid::Modulator::setPayloadMCS,
            "Payload MCS")
        .def("modulate",
            modulate,
            "Modulate a packet")
        ;

    // Export class Demodulator to Python
    py::class_<Liquid::Demodulator, std::shared_ptr<Liquid::Demodulator>>(m, "LiquidDemodulator")
        .def_property("header_mcs",
            &Liquid::Demodulator::getHeaderMCS,
            &Liquid::Demodulator::setHeaderMCS, "Header MCS")
        .def_property_readonly("soft_header",
            &Liquid::Demodulator::getSoftHeader,
            "Use soft decoding for header")
        .def_property_readonly("soft_payload",
            &Liquid::Demodulator::getSoftPayload,
            "Use soft decoding for payload")
        .def("reset",
            &Liquid::Demodulator::reset,
            "Reset demodulator state")
        .def("demodulate",
            demodulate,
            "Demodulate a signal")
        ;

    // Export class OFDMModulator to Python
    py::class_<Liquid::OFDMModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::OFDMModulator>>(m, "OFDMModulator")
        .def(py::init<unsigned,
                      unsigned,
                      unsigned>())
        .def(py::init<unsigned,
                      unsigned,
                      unsigned,
                      const std::vector<unsigned char>&>())
        ;

    // Export class OFDMDemodulator to Python
    py::class_<Liquid::OFDMDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::OFDMDemodulator>>(m, "OFDMDemodulator")
        .def(py::init<bool,
                      bool,
                      unsigned,
                      unsigned,
                      unsigned>())
       .def(py::init<bool,
                     bool,
                     unsigned,
                     unsigned,
                     unsigned,
                     const std::vector<unsigned char>&>())
        ;

#if defined(PYTHON_EXPORT_MULTIOFDM)
    // Export class MultiOFDMModulator to Python
    py::class_<Liquid::MultiOFDMModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::MultiOFDMModulator>>(m, "MultiOFDMModulator")
        .def(py::init<unsigned,
                      unsigned,
                      unsigned>())
        .def(py::init<unsigned,
                      unsigned,
                      unsigned,
                      const std::vector<unsigned char>&>())
        ;

    // Export class MultiOFDMDemodulator to Python
    py::class_<Liquid::MultiOFDMDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::MultiOFDMDemodulator>>(m, "MultiOFDMDemodulator")
        .def(py::init<bool,
                      bool,
                      unsigned,
                      unsigned,
                      unsigned>())
        .def(py::init<bool,
                      bool,
                      unsigned,
                      unsigned,
                      unsigned,
                      const std::vector<unsigned char>&>())
        ;
#endif /* defined(PYTHON_EXPORT_MULTIOFDM) */

    // Export class FlexFrameModulator to Python
    py::class_<Liquid::FlexFrameModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::FlexFrameModulator>>(m, "FlexFrameModulator")
        .def(py::init<>())
        ;

    // Export class FlexFrameDemodulator to Python
    py::class_<Liquid::FlexFrameDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::FlexFrameDemodulator>>(m, "FlexFrameDemodulator")
        .def(py::init<bool,
                      bool>())
        ;

    // Export class NewFlexFrameModulator to Python
    py::class_<Liquid::NewFlexFrameModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::NewFlexFrameModulator>>(m, "NewFlexFrameModulator")
        .def(py::init<>())
        ;

    // Export class FlexFrameDemodulator to Python
    py::class_<Liquid::NewFlexFrameDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::NewFlexFrameDemodulator>>(m, "NewFlexFrameDemodulator")
        .def(py::init<bool,
                      bool>())
        ;
}
