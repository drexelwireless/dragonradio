// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "WorkQueue.hh"
#include "phy/FlexFrame.hh"
#include "phy/Gain.hh"
#include "phy/NewFlexFrame.hh"
#include "phy/OFDM.hh"
#include "phy/PHY.hh"
#include "python/PyModules.hh"

using fc32 = std::complex<float>;

using PacketDemodulator = PHY::PacketDemodulator;
using PacketModulator = PHY::PacketModulator;

#define T PacketModulator
#define TNAME "PacketModulator"
#include "python/py_shared_ptr.hh"
#undef T
#undef TNAME

#define T PacketDemodulator
#define TNAME "PacketDemodulator"
#include "python/py_shared_ptr.hh"
#undef T
#undef TNAME

class PyPHY : public PHY {
public:
    using PHY::PHY;

    unsigned getMinRXRateOversample(void) const override
    {
        PYBIND11_OVERLOAD_PURE(
            unsigned,
            PHY,
            getMinRXRateOversample,
        );
    }

    unsigned getMinTXRateOversample(void) const override
    {
        PYBIND11_OVERLOAD_PURE(
            unsigned,
            PHY,
            getMinTXRateOversample,
        );
    }

    size_t getModulatedSize(mcsidx_t mcsidx, size_t n) override
    {
        PYBIND11_OVERLOAD_PURE(
            size_t,
            PHY,
            getModulatedSize,
            mcsidx,
            n
        );
    }

    std::shared_ptr<PacketModulator> mkPacketModulator(void) override
    {
        PYBIND11_OVERLOAD_PURE(
            std::shared_ptr<PacketModulator>,
            PHY,
            mkPacketModulator,
        );
    }

    std::shared_ptr<PacketDemodulator> mkPacketDemodulator(unsigned chanidx, const Channel &channel) override
    {
        PYBIND11_OVERLOAD_PURE(
            std::shared_ptr<PacketDemodulator>,
            PHY,
            mkPacketDemodulator,
            chanidx,
            channel
        );
    }

    void updateAutoGain(const std::shared_ptr<NetPacket> &pkt,
                        float g,
                        const std::shared_ptr<IQBuf> &iqbuf)
    {
        AutoGain &autogain = mcs_table[pkt->mcsidx].autogain;

        if (autogain.needCalcAutoSoftGain0dBFS())
            work_queue.submit(&AutoGain::autoSoftGain0dBFS, &autogain, g, iqbuf);
    }

    std::vector<PHY::MCSEntry> &getMCSTable()
    {
        return mcs_table;
    }

    void setMCSTable(const std::vector<std::pair<std::shared_ptr<MCS>, AutoGain>> &table)
    {
        mcs_table_.resize(table.size());
        mcs_table.resize(table.size());

        for (unsigned i = 0; i < table.size(); ++i) {
            mcs_table_[i] = table[i].first;

            mcs_table[i].mcs = mcs_table_[i].get();
            mcs_table[i].autogain = table[i].second;
        }
    }

protected:
    /** @brief Actual MCS objects referred to by mcs_table */
    std::vector<std::shared_ptr<MCS>> mcs_table_;
};

class PyPacketModulator : public PacketModulator {
public:
    using PacketModulator::PacketModulator;

    void modulate(std::shared_ptr<NetPacket> pkt,
                  const float gain,
                  ModPacket &mpkt) override
    {
        py::gil_scoped_acquire gil;
        py::function           overload = py::get_overload(this, "modulate");

        if (overload) {
            py::object                 obj = overload(pkt, gain);
            std::shared_ptr<ModPacket> mpktp = obj.cast<std::shared_ptr<ModPacket>>();

            mpkt = *mpktp;
        }
    }
};

class PyPacketDemodulator : public PacketDemodulator {
public:
    using PacketDemodulator::PacketDemodulator;

    bool isFrameOpen(void) override
    {
        PYBIND11_OVERLOAD_PURE(
            bool,
            PacketDemodulator,
            isFrameOpen,
        );
    }

    void reset(const Channel &channel) override
    {
        PYBIND11_OVERLOAD_PURE(
            void,
            PacketDemodulator,
            reset,
            channel
        );
    }

    void timestamp(const MonoClock::time_point &timestamp,
                   std::optional<ssize_t> snapshot_off,
                   ssize_t offset,
                   size_t delay,
                   float rate,
                   float rx_rate) override
    {
        PYBIND11_OVERLOAD_PURE(
            void,
            PacketDemodulator,
            timestamp,
            timestamp,
            snapshot_off,
            offset,
            delay,
            rate,
            rx_rate
        );
    }

    void demodulate(const std::complex<float>* data,
                    size_t count) override
    {
        py::gil_scoped_acquire gil;
        py::function           overload = py::get_overload(this, "demodulate");

        if (overload) {
            py::array_t<fc32> buf(count, data);
            py::list          pkts = overload(buf);

            for (py::handle pkt : pkts) {
                std::shared_ptr<RadioPacket> rpkt = pkt.cast<std::shared_ptr<RadioPacket>>();

                if (rpkt)
                    callback_(std::move(rpkt));
            }
        }
    }
};

void exportPHYs(py::module &m)
{
    // Export class Gain to Python
    py::class_<Gain, std::shared_ptr<Gain>>(m, "Gain")
        .def_property("lin",
            &Gain::getLinearGain,
            &Gain::setLinearGain,
            "float: Linear gain (multiplicative factor)")
        .def_property("dB",
            &Gain::getDbGain,
            &Gain::setDbGain,
            "float: Logarithmic gain (dB)")
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
            "float: Soft TX gain (multiplicative factor)")
        .def_property("soft_tx_gain_0dBFS",
            &AutoGain::getSoftTXGain0dBFS,
            &AutoGain::setSoftTXGain0dBFS,
            "float: Soft TX gain (dBFS)")
        .def_property("auto_soft_tx_gain_clip_frac",
            &AutoGain::getAutoSoftTXGainClipFrac,
            &AutoGain::setAutoSoftTXGainClipFrac,
            "float: Clipping threshold for automatic TX soft gain")
        .def("recalc0dBFSEstimate",
            &AutoGain::recalc0dBFSEstimate,
            "Reset the 0dBFS estimate",
            py::arg("nsamples"))
        ;

    // Export class MCSEntry to Python
    py::class_<PHY::MCSEntry, std::shared_ptr<PHY::MCSEntry>>(m, "MCSEntry")
        .def_readonly("mcs",
            &PHY::MCSEntry::mcs,
            "MCS: Modulation and coding scheme")
        .def_readonly("autogain",
            &PHY::MCSEntry::autogain,
            "AutoGain: AutoGain for MCS")
        ;

    // Export class ModPacket to Python
    py::class_<ModPacket, std::shared_ptr<ModPacket>>(m, "ModPacket", "A modulated packet")
        .def(py::init<>())
        .def_readwrite("chanidx",
            &ModPacket::chanidx,
            "int: Index of channel")
        .def_readwrite("channel",
            &ModPacket::channel,
            "Channel: Channel")
        .def_readwrite("start",
            &ModPacket::start,
            "int: Offset of start of packet from start of slot, in number of samples")
        .def_readwrite("offset",
            &ModPacket::offset,
            "int: Offset of start of packet from beginning of sample buffer")
        .def_readwrite("nsamples",
            &ModPacket::nsamples,
            "int: Number of modulated samples")
        .def_readwrite("samples",
            &ModPacket::samples,
            "IQBuffer: Buffer containing the modulated samples")
        .def_readwrite("pkt",
            &ModPacket::pkt,
            "Packet: The un-modulated packet")
        ;

    // Export class PHY to Python
    py::class_<PHY, PyPHY, std::shared_ptr<PHY>>(m, "PHY")
        .def(py::init_alias<>())
        .def_property("mcs_table",
            [](PyPHY &self)
            {
                return self.getMCSTable();
            },
            [](PyPHY &self, const std::vector<std::pair<std::shared_ptr<MCS>, AutoGain>> &table)
            {
                self.setMCSTable(table);
            },
            "Table of modulation and coding schemes")
        .def_property_readonly("min_rx_rate_oversample",
            &PHY::getMinRXRateOversample,
            "Minimum oversample rate needed for RX")
        .def_property_readonly("min_tx_rate_oversample",
            &PHY::getMinTXRateOversample,
            "Minimum oversample rate needed for TX")
        .def("getMinRXRateOversample",
            &PHY::getMinRXRateOversample)
        .def("getMinTXRateOversample",
            &PHY::getMinTXRateOversample)
        .def("getModulatedSize",
            &PHY::getModulatedSize)
        .def("mkPacketModulator",
            &PHY::mkPacketModulator)
        .def("mkPacketDemodulator",
            &PHY::mkPacketDemodulator)
        .def("updateAutoGain",
            [](PyPHY &self,
               const std::shared_ptr<NetPacket> &pkt,
               float g,
               const std::shared_ptr<IQBuf> &iqbuf)
            {
                self.updateAutoGain(pkt, g, iqbuf);
            })
        .def_static("mkRadioPacket",
            [](const Header &hdr, std::optional<py::bytes> payload) -> std::shared_ptr<RadioPacket>
            {
                if (payload) {
                    std::string s = *payload;

                    return PHY::mkRadioPacket(true, true, hdr, s.size(), reinterpret_cast<unsigned char*>(s.data()));
                } else {
                    return PHY::mkRadioPacket(true, false, hdr, 0, nullptr);
                }
            })
        .def_property_static("team",
            [](py::object) { return PHY::getTeam(); },
            [](py::object, uint8_t team) { PHY::setTeam(team); },
            "int: Team")
        .def_property_static("node_id",
            [](py::object) { return PHY::getNodeId(); },
            [](py::object, NodeId id) { PHY::setNodeId(id); },
            "int: Node ID")
        .def_property_static("log_invalid_headers",
            [](py::object) { return PHY::getLogInvalidHeaders(); },
            [](py::object, bool log) { PHY::setLogInvalidHeaders(log); },
            "bool: Log invalid headers?")
        .def_property_static("snapshot_collector",
            nullptr,
            [](py::object, std::shared_ptr<SnapshotCollector> collector) { PHY::setSnapshotCollector(collector); },
            "Optional[SnapshotCollector]: Snapshot collector")
        ;

    // Export class PacketModulator to Python
    py::class_<PacketModulator, PyPacketModulator, std::shared_ptr<PacketModulator>>(m, "PacketModulator")
        .def(py::init<PHY&>())
        .def("modulate",
            &PacketModulator::modulate)
        ;

    // Export class PacketModulator to Python
    py::class_<PacketDemodulator, PyPacketDemodulator, std::shared_ptr<PacketDemodulator>>(m, "PacketDemodulator")
        .def(py::init<PHY&,
                      unsigned,
                      const Channel&>())
        .def("isFrameOpen",
            &PacketDemodulator::isFrameOpen)
        .def("reset",
            &PacketDemodulator::reset)
        .def("timestamp",
            &PacketDemodulator::timestamp)
        .def("demodulate",
            &PacketDemodulator::demodulate)
        ;

    // Export class PHYChannel to Python
    py::class_<PHYChannel, std::shared_ptr<PHYChannel>>(m, "PHYChannel")
        .def(py::init<const Channel&,
                      std::shared_ptr<PHY>,
                      const std::vector<evm_thresh_t>&>())
        .def_readwrite("channel",
            &PHYChannel::channel,
            "Channel: Channel")
        .def_readwrite("phy",
            &PHYChannel::phy,
            "PHY: PHY for channel")
        .def_readwrite("evm_thresh",
            &PHYChannel::evm_thresh,
            "EVM threshold table")
        .def_readwrite("I",
            &PHYChannel::I,
            "Interpolation rate")
        .def_readwrite("D",
            &PHYChannel::D,
            "Decimation rate")
        .def_readwrite("taps",
            &PHYChannel::taps,
            "FIR filter taps")
        ;

    // Export vector of PHYChannels
    py::bind_vector<std::vector<PHYChannel>>(m, "PHYChannels");
}

void exportLiquidPHYs(py::module &m)
{
    // Export class liquid::PHY to Python
    py::class_<liquid::PHY, PHY, std::shared_ptr<liquid::PHY>>(m, "LiquidPHY")
        .def_property_readonly("header_mcs",
            &liquid::PHY::getHeaderMCS,
            "MCS: Header MCS")
        .def_property_readonly("soft_header",
            &liquid::PHY::getSoftHeader,
            "bool: Use soft decoding for header")
        .def_property_readonly("soft_payload",
            &liquid::PHY::getSoftPayload,
            "bool: Use soft decoding for payload")
        ;

    // Export class FlexFrame to Python
    py::class_<liquid::FlexFrame, liquid::PHY, std::shared_ptr<liquid::FlexFrame>>(m, "FlexFrame", "Liquid FlexFrame PHY")
        .def(py::init<const liquid::MCS&,
                      const std::vector<std::pair<liquid::MCS, AutoGain>>&,
                      bool,
                      bool>(),
            "Initialize FlexFrame PHY",
            py::arg("header_mcs"),
            py::arg("mcs_table"),
            py::arg("soft_header"),
            py::arg("soft_payload"))
        ;

    // Export class NewFlexFrame to Python
    py::class_<liquid::NewFlexFrame, liquid::PHY, std::shared_ptr<liquid::NewFlexFrame>>(m, "NewFlexFrame", "Liquid \"new\" FlexFrame PHY")
        .def(py::init<const liquid::MCS&,
                      const std::vector<std::pair<liquid::MCS, AutoGain>>&,
                      bool,
                      bool>(),
            "Initialize \"new\" FlexFrame PHY",
            py::arg("header_mcs"),
            py::arg("mcs_table"),
            py::arg("soft_header"),
            py::arg("soft_payload"))
        ;

    // Export class OFDM to Python
    py::class_<liquid::OFDM, liquid::PHY, std::shared_ptr<liquid::OFDM>>(m, "OFDM", "Liquid OFDM PHY")
        .def(py::init<const liquid::MCS&,
                      const std::vector<std::pair<liquid::MCS, AutoGain>>&,
                      bool,
                      bool,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      const std::optional<std::string>&>(),
            "Initialize OFDM PHY",
            py::arg("header_mcs"),
            py::arg("mcs_table"),
            py::arg("soft_header"),
            py::arg("soft_payload"),
            py::arg("M"),
            py::arg("cp_len"),
            py::arg("taper_len"),
            py::arg("subcarriers"))
        .def_property_readonly("subcarriers",
            [](std::shared_ptr<liquid::OFDM> self)
            {
                return static_cast<std::string>(self->getSubcarriers());
            },
            "str: Subcarriers")
        ;
}
