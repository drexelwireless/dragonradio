// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "Header.hh"
#include "python/PyModules.hh"

void exportHeader(py::module &m)
{
    // Export Header::Flags class to Python
    py::class_<Header::Flags>(m, "HeaderFlags")
        .def_property("syn",
            [](Header::Flags &self) { return self.syn; },
            [](Header::Flags &self, uint8_t f) { self.syn = f; },
            "bool: SYN flag")
        .def_property("ack",
            [](Header::Flags &self) { return self.ack; },
            [](Header::Flags &self, uint8_t f) { self.ack = f; },
            "bool: ACK flag")
        .def_property("has_seq",
            [](Header::Flags &self) { return self.has_seq; },
            [](Header::Flags &self, uint8_t f) { self.has_seq = f; },
            "bool: Is the packet sequenced?")
        .def_property("has_control",
            [](Header::Flags &self) { return self.has_control; },
            [](Header::Flags &self, uint8_t f) { self.has_control = f; },
            "bool: Does packet have control information?")
        .def_property("compressed",
            [](Header::Flags &self) { return self.compressed; },
            [](Header::Flags &self, uint8_t f) { self.compressed = f; },
            "bool: Is packet compressed?")
        .def_property("team",
            [](Header::Flags &self) { return self.team; },
            [](Header::Flags &self, uint8_t f) { self.team = f; },
            "int: Team")
        .def("__repr__", [](const Header::Flags& self) {
            return py::str("HeaderFlags(syn={:d}, ack={:d}, has_seq={:d}, has_control=={:d}, compressed=={:d}, team=={:d})").\
            format(self.syn, self.ack, self.has_seq, self.has_control, self.compressed, self.team);
         })
        ;

    // Export class Header to Python
    py::class_<Header, std::shared_ptr<Header>>(m, "Header")
        .def(py::init<>())
        .def(py::init([](NodeId curhop, NodeId nexthop, Seq::uint_type seq){
            return Header{ curhop, nexthop, Seq{seq}, {0}};
        }),
            py::arg("curhop"),
            py::arg("nexthop"),
            py::arg("seq"))
        .def_readwrite("curhop",
            &Header::curhop,
            "NodeId: Current hop")
        .def_readwrite("nexthop",
            &Header::nexthop,
            "NodeId: Next hop")
        .def_readwrite("seq",
            &Header::seq,
            "Seq: Packet sequence number")
        .def_readwrite("flags",
            &Header::flags,
            "HeaderFlags: Flags")
        .def("__repr__", [](const Header& self) {
            return py::str("Header(curhop={:d}, nexthop={:d}, seq={:d}, flags={})").\
            format(self.curhop, self.nexthop, static_cast<Seq::uint_type>(self.seq), self.flags);
         })
        ;

    // Export class ExtendedHeader to Python
    py::class_<ExtendedHeader, std::shared_ptr<ExtendedHeader>>(m, "ExtendedHeader")
        .def(py::init([](NodeId src, NodeId dest, Seq::uint_type ack, uint16_t data_len){
            return ExtendedHeader{ src, dest, Seq{ack}, data_len};
        }),
            py::arg("src"),
            py::arg("dest"),
            py::arg("ack"),
            py::arg("data_len"))
        .def_readwrite("src",
            &ExtendedHeader::src,
            "NodeId: Source node")
        .def_readwrite("dest",
            &ExtendedHeader::dest,
            "NodeId: Destination node")
        .def_readwrite("ack",
            &ExtendedHeader::dest,
            "Seq: Sequence number being ACK'ed")
        .def_readwrite("data_len",
            &ExtendedHeader::data_len,
            "int: Length of packet data")
        ;
}
