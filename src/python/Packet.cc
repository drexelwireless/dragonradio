// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "Header.hh"
#include "Packet.hh"
#include "python/PyModules.hh"

using fc32 = std::complex<float>;

void exportPacket(py::module &m)
{
    // Export Packet::InternalFlags class to Python
    py::class_<Packet::InternalFlags>(m, "PacketInternalFlags")
        .def_property("invalid_header",
            [](Packet::InternalFlags &self) { return self.invalid_header; },
            [](Packet::InternalFlags &self, uint8_t f) { self.invalid_header = f; },
            "Is header invalid?")
        .def_property("invalid_payload",
            [](Packet::InternalFlags &self) { return self.invalid_payload; },
            [](Packet::InternalFlags &self, uint8_t f) { self.invalid_payload = f; },
            "Is payload invalid?")
        .def_property("retransmission",
            [](Packet::InternalFlags &self) { return self.retransmission; },
            [](Packet::InternalFlags &self, uint8_t f) { self.retransmission = f; },
            "Is this a retransmission?")
        .def_property("assigned_seq",
            [](Packet::InternalFlags &self) { return self.assigned_seq; },
            [](Packet::InternalFlags &self, uint8_t f) { self.assigned_seq = f; },
            "Set if packet has been assigned a sequence number")
        .def_property("has_selective_ack",
            [](Packet::InternalFlags &self) { return self.has_selective_ack; },
            [](Packet::InternalFlags &self, uint8_t f) { self.has_selective_ack = f; },
            "Set if packet contains a selective ACK")
        ;

    // Export class Packet to Python
    py::class_<Packet, std::shared_ptr<Packet>>(m, "Packet")
        .def_readwrite("hdr",
            &Packet::hdr,
            "Packet header")
        .def_readwrite("flow_uid",
            &Packet::flow_uid,
            "Flow UID")
        .def_readwrite("timestamp",
            &Packet::timestamp,
            "Packet timestamp")
        .def_readwrite("payload_size",
            &Packet::payload_size,
            "Payload size")
        .def_property_readonly("payload",
            [](Packet &self)
            {
                return py::bytes(reinterpret_cast<char*>(self.data()), self.size());
            },
            "Payload")
        .def_readwrite("internal_flags",
            &Packet::internal_flags,
            "Internal flags")
        .def_property("ehdr",
            [](Packet &self) { return self.ehdr(); },
            [](Packet &self, const ExtendedHeader &ehdr) { self.ehdr() = ehdr; },
            "Extended header")
        ;

    // Export class NetPacket to Python
    py::class_<NetPacket, Packet, std::shared_ptr<NetPacket>>(m, "NetPacket")
        .def_readwrite("deadline",
            &NetPacket::deadline,
            "Packet delivery deadline")
        .def_readwrite("mcsidx",
            &NetPacket::mcsidx,
            "MCS to use")
        .def_readwrite("g",
            &NetPacket::g,
            "Multiplicative TX gain")
        ;

    // Export class RadioPacket to Python
    py::class_<RadioPacket, Packet, std::shared_ptr<RadioPacket>>(m, "RadioPacket")
        .def(py::init<const Header &>())
        .def(py::init([](const Header &hdr, py::bytes payload) {
                std::string s = payload;

                return std::make_shared<RadioPacket>(hdr, reinterpret_cast<unsigned char*>(s.data()), s.size());
            }))
        .def_readwrite("evm",
            &RadioPacket::evm,
            "EVM")
        .def_readwrite("rssi",
            &RadioPacket::rssi,
            "RSSI")
        .def_readwrite("cfo",
            &RadioPacket::cfo,
            "CFO")
        .def_readwrite("channel",
            &RadioPacket::channel,
            "Channel")
        ;
}
