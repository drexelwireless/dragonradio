#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "liquid/FlexFrame.hh"
#include "liquid/NewFlexFrame.hh"
#include "liquid/OFDM.hh"
#include "python/PyModules.hh"

using FrameStats = framesyncstats_s;

using Demod = std::tuple<std::optional<Header>,
                         std::optional<py::bytes>,
                         FrameStats>;

std::vector<Demod> demodulate(Liquid::Demodulator &demod,
                              py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> sig)
{
    // We use std::string to store payloads and batch convert once demodulation
    // is done
    using LocalDemod = std::tuple<std::optional<Header>,
                                  std::optional<std::string>,
                                  FrameStats>;

    std::vector<LocalDemod> local_pkts;
    size_t                  sample_offset = 0;

    // Get the signal to demodulate
    auto buf = sig.request();

    // Release the GIL and demodulate
    {
        py::gil_scoped_release gil;

        Demodulator::callback_t cb = [&](bool header_test,
                                         bool header_valid,
                                         bool payload_valid,
                                         const Header* header,
                                         void *payload,
                                         size_t payload_len,
                                         void *stats_)
        {
            std::optional<Header>      h;
            std::optional<std::string> p;
            framesyncstats_s           stats = *((framesyncstats_s*) stats_);

            if (header_test)
                return 1;

            if (header_valid)
                h = *((Header *) header);

            if (payload_valid)
                p = std::string(reinterpret_cast<char*>(payload), payload_len);

            stats.start_counter += sample_offset;
            stats.end_counter += sample_offset;
            sample_offset += stats.sample_counter;

            local_pkts.emplace_back(std::make_tuple(h, p, stats));

            return 0;
        };

        demod.demodulate(static_cast<std::complex<float>*>(buf.ptr), buf.size, cb);
    }

    // Now we have the GIL, so we can convert payloads to Python bytes objects
    {
        std::vector<Demod> pkts;

        for (auto [h, p, stats] : local_pkts)
            pkts.emplace_back(std::make_tuple(std::move(h), p ? std::make_optional(py::bytes(*p)) : std::nullopt, std::move(stats)));

        return pkts;
    }
}

void exportLiquid(py::module &m)
{
    // Create enum type CRCScheme for liquid CRC schemes
    py::enum_<crc_scheme> crc(m, "CRCScheme");

    crc.def(py::init([](std::string value) -> crc_scheme {
            auto crc = liquid_getopt_str2crc(value.c_str());
            if (crc == LIQUID_CRC_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type CRCScheme");
            return crc;
        }));

    py::implicitly_convertible<py::str, crc_scheme>();

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

    py::implicitly_convertible<py::str, fec_scheme>();

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

    py::implicitly_convertible<py::str, modulation_scheme>();

    for (unsigned int i = 0; i < LIQUID_MODEM_NUM_SCHEMES; ++i)
        ms.value(modulation_types[i].name, static_cast<modulation_scheme>(i));

    ms.export_values();

    // Create export OFDM frame subcarrier types
    m.attr("kSCTypeNull") = py::int_(OFDMFRAME_SCTYPE_NULL);
    m.attr("kSCTypePilot") = py::int_(OFDMFRAME_SCTYPE_PILOT);
    m.attr("kSCTypeData") = py::int_(OFDMFRAME_SCTYPE_DATA);

    // Export class MCS to Python
    py::class_<Liquid::MCS, MCS, std::shared_ptr<Liquid::MCS>>(m, "MCS")
        .def(py::init<>())
        .def(py::init<crc_scheme,
                      fec_scheme,
                      fec_scheme,
                      modulation_scheme>())
        .def_readwrite("check", &Liquid::MCS::check, "Data validity check")
        .def_readwrite("fec0", &Liquid::MCS::fec0, "Inner FEC")
        .def_readwrite("fec1", &Liquid::MCS::fec1, "Outer FEC")
        .def_readwrite("ms", &Liquid::MCS::ms, "Modulation scheme")
        .def("__repr__", [](const Liquid::MCS& self) {
            return py::str("MCS(check={}, fec0={}, fec1={}, ms={})").format(self.check, self.fec0, self.fec1, self.ms);
         })
    ;

    // Export class FrameStats to Python
    py::class_<FrameStats>(m, "FrameStats")
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
        .def_readonly("start_counter",
            &FrameStats::start_counter,
            "Sample offset of start of demodulated packet")
        .def_readonly("end_counter",
            &FrameStats::end_counter,
            "Sample offset of end of demodulated packet")
        .def("__repr__", [](const FrameStats& self) {
            return py::str("FrameStats(evm={:0.2g}, rssi={:0.2g}, cfo={:0.2g}, mod_scheme={}, mod_bps={}, check={}, fec0={}, fec1={}, start={}, end={})").\
            format(self.evm, self.rssi, self.cfo,
                   static_cast<modulation_scheme>(self.mod_scheme),
                   self.mod_bps,
                   static_cast<crc_scheme>(self.check),
                   static_cast<fec_scheme>(self.fec0),
                   static_cast<fec_scheme>(self.fec1),
                   self.start_counter,
                   self.end_counter);
         })
        ;

    // Export class Liquid::Modulator to Python
    py::class_<Liquid::Modulator, Modulator, std::shared_ptr<Liquid::Modulator>>(m, "LiquidModulator")
        .def_property("header_mcs",
            &Liquid::Modulator::getHeaderMCS,
            &Liquid::Modulator::setHeaderMCS,
            "Header MCS")
        .def_property("payload_mcs",
            &Liquid::Modulator::getPayloadMCS,
            &Liquid::Modulator::setPayloadMCS,
            "Payload MCS")
        ;

    // Export class Demodulator to Python
    py::class_<Liquid::Demodulator, Demodulator, std::shared_ptr<Liquid::Demodulator>>(m, "LiquidDemodulator")
        .def_property("header_mcs",
            &Liquid::Demodulator::getHeaderMCS,
            &Liquid::Demodulator::setHeaderMCS, "Header MCS")
        .def_property_readonly("soft_header",
            &Liquid::Demodulator::getSoftHeader,
            "Use soft decoding for header")
        .def_property_readonly("soft_payload",
            &Liquid::Demodulator::getSoftPayload,
            "Use soft decoding for payload")
        .def("demodulate",
            demodulate,
            "Demodulate a signal")
        ;

    // We need to use the py::multiple_inheritance{} annotation because these
    // calsses have a virtual base class.
    // See:
    //   https://github.com/pybind/pybind11/issues/1256
    //   https://github.com/yeganer/pybind11/commit/6a82cfab236302951966b6d39f13c738bafc324d

    // Export class OFDMModulator to Python
    py::class_<Liquid::OFDMModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::OFDMModulator>>(m, "OFDMModulator", py::multiple_inheritance{})
        .def(py::init([](const Liquid::MCS &header_mcs,
                         unsigned M,
                         unsigned cp_len,
                         unsigned taper_len)
            {
                return std::make_shared<Liquid::OFDMModulator>(header_mcs,
                                                               M,
                                                               cp_len,
                                                               taper_len,
                                                               std::nullopt);
            }))
        .def(py::init([](const Liquid::MCS &header_mcs,
                         unsigned M,
                         unsigned cp_len,
                         unsigned taper_len,
                         std::optional<const std::string> &p)
            {
                return std::make_shared<Liquid::OFDMModulator>(header_mcs,
                                                               M,
                                                               cp_len,
                                                               taper_len,
                                                               p ? std::make_optional(Liquid::OFDMSubcarriers(!p)) : std::nullopt);
            }))
        ;

    // Export class OFDMDemodulator to Python
    py::class_<Liquid::OFDMDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::OFDMDemodulator>>(m, "OFDMDemodulator", py::multiple_inheritance{})
        .def(py::init([](const Liquid::MCS &header_mcs,
                         bool soft_header,
                         bool soft_payload,
                         unsigned M,
                         unsigned cp_len,
                         unsigned taper_len)
            {
                return std::make_shared<Liquid::OFDMDemodulator>(header_mcs,
                                                                 soft_header,
                                                                 soft_payload,
                                                                 M,
                                                                 cp_len,
                                                                 taper_len,
                                                                 std::nullopt);
            }))
        .def(py::init([](const Liquid::MCS &header_mcs,
                         bool soft_header,
                         bool soft_payload,
                         unsigned M,
                         unsigned cp_len,
                         unsigned taper_len,
                         std::optional<const std::string> &p)
            {
                return std::make_shared<Liquid::OFDMDemodulator>(header_mcs,
                                                                 soft_header,
                                                                 soft_payload,
                                                                 M,
                                                                 cp_len,
                                                                 taper_len,
                                                                 p ? std::make_optional(Liquid::OFDMSubcarriers(!p)) : std::nullopt);
            }))
        ;

    // Export class FlexFrameModulator to Python
    py::class_<Liquid::FlexFrameModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::FlexFrameModulator>>(m, "FlexFrameModulator", py::multiple_inheritance{})
        .def(py::init<const Liquid::MCS&>())
        ;

    // Export class FlexFrameDemodulator to Python
    py::class_<Liquid::FlexFrameDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::FlexFrameDemodulator>>(m, "FlexFrameDemodulator", py::multiple_inheritance{})
        .def(py::init<const Liquid::MCS&,
                      bool,
                      bool>())
        ;

    // Export class NewFlexFrameModulator to Python
    py::class_<Liquid::NewFlexFrameModulator,
               Liquid::Modulator,
               std::shared_ptr<Liquid::NewFlexFrameModulator>>(m, "NewFlexFrameModulator", py::multiple_inheritance{})
        .def(py::init<const Liquid::MCS&>())
        ;

    // Export class FlexFrameDemodulator to Python
    py::class_<Liquid::NewFlexFrameDemodulator,
               Liquid::Demodulator,
               std::shared_ptr<Liquid::NewFlexFrameDemodulator>>(m, "NewFlexFrameDemodulator", py::multiple_inheritance{})
        .def(py::init<const Liquid::MCS&,
                      bool,
                      bool>())
        ;
}
