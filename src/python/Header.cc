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
            "SYN flag")
        .def_property("ack",
            [](Header::Flags &self) { return self.ack; },
            [](Header::Flags &self, uint8_t f) { self.ack = f; },
            "ACK flag")
        .def_property("has_data",
            [](Header::Flags &self) { return self.has_data; },
            [](Header::Flags &self, uint8_t f) { self.has_data = f; },
            "Does packet have data?")
        .def_property("has_control",
            [](Header::Flags &self) { return self.has_control; },
            [](Header::Flags &self, uint8_t f) { self.has_control = f; },
            "Does packet have control information?")
        .def_property("compressed",
            [](Header::Flags &self) { return self.compressed; },
            [](Header::Flags &self, uint8_t f) { self.compressed = f; },
            "Is packet compressed?")
        .def("__repr__", [](const Header::Flags& self) {
            return py::str("HeaderFlags(syn={:d}, ack={:d}, has_data={:d}, has_control=={:d}, compressed=={:d})").\
            format(self.syn, self.ack, self.has_data, self.has_control, self.compressed);
         })
        ;

    // Export class Header to Python
    py::class_<Header, std::shared_ptr<Header>>(m, "Header")
        .def(py::init<>())
        .def(py::init([](NodeId curhop, NodeId nexthop, Seq::uint_type seq){
            return Header{ curhop, nexthop, Seq{seq}, 0};
        }))
        .def_readwrite("curhop",
            &Header::curhop,
            "Current hop")
        .def_readwrite("nexthop",
            &Header::nexthop,
            "Next hop")
        .def_readwrite("seq",
            &Header::seq,
            "Packet sequence number")
        .def_readwrite("flags",
            &Header::flags,
            "Flag")
        .def("__repr__", [](const Header& self) {
            return py::str("Header(curhop={:d}, nexthop={:d}, seq={:d}, flags={})").\
            format(self.curhop, self.nexthop, static_cast<Seq::uint_type>(self.seq), self.flags);
         })
        ;
}
