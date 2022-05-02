// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "RadioNet.hh"
#include "python/PyModules.hh"

void exportRadioNet(py::module &m)
{
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
