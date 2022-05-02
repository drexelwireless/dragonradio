// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "Node.hh"
#include "python/PyModules.hh"

void exportNode(py::module &m)
{
    // Export class GPSLocation to Python
    py::class_<GPSLocation, std::shared_ptr<GPSLocation>>(m, "GPSLocation")
        .def_readwrite("lon",
            &GPSLocation::lon,
            "Longitude")
        .def_readwrite("lat",
            &GPSLocation::lat,
            "Latitude")
        .def_readwrite("alt",
            &GPSLocation::alt,
            "Altitude")
        .def_readwrite("timestamp",
            &GPSLocation::timestamp,
            "Timestamp of last update")
        .def("__repr__", [](const GPSLocation& self) {
            return py::str("GPSLocation(lat={},lon={},alt={},timestamp={})").format(self.lat, self.lon, self.alt, self.timestamp);
         })
        ;

    // Export class Node to Python
    py::class_<Node, std::shared_ptr<Node>>(m, "Node")
        .def_readonly("id",
            &Node::id,
            "Node ID")
        .def_property_readonly("loc",
            [](Node &self) -> GPSLocation& {
                return self.loc;
            },
            py::return_value_policy::reference_internal)
        .def_readwrite("is_gateway",
            &Node::is_gateway,
            "Flag indicating whether or not this node is the gateway")
        .def_readonly("emcon",
            &Node::emcon,
            "Flag indicating whether or not this node is subject to emissions control")
        .def_readonly("unreachable",
            &Node::unreachable,
            "Flag indicating whether or not this node is unreachable")
        .def_readwrite("g",
            &Node::g,
            "Soft TX gain (multiplicative)")
        .def_property("soft_tx_gain",
            &Node::getSoftTXGain,
            &Node::setSoftTXGain,
            "Soft TX gain (dBFS)")
        ;
}
