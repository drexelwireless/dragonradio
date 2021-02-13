// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "RadioNet.hh"
#include "python/PyModules.hh"

void exportRadioNet(py::module &m)
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
        .def_readwrite("can_transmit",
            &Node::can_transmit,
            "Flag indicating whether or not this node can transmit")
        .def_readwrite("g",
            &Node::g,
            "Soft TX gain (multiplicative)")
        .def_property("soft_tx_gain",
            &Node::getSoftTXGain,
            &Node::setSoftTXGain,
            "Soft TX gain (dBFS)")
        .def_readonly("mcsidx",
            &Node::mcsidx,
            "MCS index")
        .def_property_readonly("timestamps",
            [](Node &node) {
                std::lock_guard<std::mutex> lock(node.timestamps_mutex);

                return node.timestamps;
            },
            "Timestamps received from this node")
        ;

    // Export class RadioNet to Python
    py::class_<RadioNet, std::shared_ptr<RadioNet>>(m, "RadioNet")
        .def(py::init<std::shared_ptr<TunTap>,
                      NodeId>())
        .def_property_readonly("this_node_id",
            &RadioNet::getThisNodeId)
        .def_property_readonly("this_node",
            &RadioNet::getThisNode,
            py::return_value_policy::reference_internal)
        .def_property_readonly("nodes",
            &RadioNet::getNodes,
            "Nodes in the network")
        .def_property_readonly("time_master",
            &RadioNet::getTimeMaster)
        .def_property("new_node_callback",
            nullptr,
            &RadioNet::setNewNodeCallback)
        .def("__getitem__",
            [](RadioNet &self, size_t i) -> Node& { return self[i]; },
            py::return_value_policy::reference_internal)
        .def("getNode",
            [](RadioNet &self, NodeId id) { return self.getNode(id); })
        .def("addNode",
            [](RadioNet &self, NodeId id) { return self.getNode(id); })
        ;
}
