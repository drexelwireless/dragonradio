// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "Neighborhood.hh"
#include "python/PyModules.hh"

void exportNeighborhood(py::module &m)
{
    // Export class Neighborhood to Python
    py::class_<Neighborhood, std::shared_ptr<Neighborhood>>(m, "Neighborhood")
        .def(py::init<std::shared_ptr<TunTap>,
                      NodeId>())
        .def_property_readonly("this_node_id",
            &Neighborhood::getThisNodeId)
        .def_property_readonly("this_node",
            &Neighborhood::getThisNode,
            py::return_value_policy::reference_internal)
        .def_property_readonly("nodes",
            &Neighborhood::getNodes,
            "Nodes in the network")
        .def_property_readonly("time_master",
            &Neighborhood::getTimeMaster)
        .def_property("new_node_callback",
            nullptr,
            &Neighborhood::setNewNodeCallback)
        .def("__getitem__",
            [](Neighborhood &self, size_t i) -> Node& { return self[i]; },
            py::return_value_policy::reference_internal)
        .def("getNode",
            [](Neighborhood &self, NodeId id) { return self.getNode(id); })
        .def("addNode",
            [](Neighborhood &self, NodeId id) { return self.getNode(id); })
        ;
}
