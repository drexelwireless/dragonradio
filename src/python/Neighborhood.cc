// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "Neighborhood.hh"
#include "python/PyModules.hh"

/* Trampoline class for Neighborhood */
class PyNeighborhoodListener : public NeighborhoodListener {
public:
    /* Inherit the constructors */
    using NeighborhoodListener::NeighborhoodListener;

    void neighborAdded(const std::shared_ptr<Node> &neighbor) override
    {
        py::gil_scoped_acquire acquire;

        PYBIND11_OVERRIDE(
            void,                 /* Return type */
            NeighborhoodListener, /* Parent class */
            neighborAdded,        /* Name of function in C++ (must match Python name) */
            neighbor              /* Argument(s) */
        );
    }

    void neighborRemoved(const std::shared_ptr<Node> &neighbor) override
    {
        py::gil_scoped_acquire acquire;

        PYBIND11_OVERRIDE(
            void,                 /* Return type */
            NeighborhoodListener, /* Parent class */
            neighborRemoved,      /* Name of function in C++ (must match Python name) */
            neighbor              /* Argument(s) */
        );
    }

    void gatewayAdded(const std::shared_ptr<Node> &neighbor) override
    {
        py::gil_scoped_acquire acquire;

        PYBIND11_OVERRIDE(
            void,                 /* Return type */
            NeighborhoodListener, /* Parent class */
            gatewayAdded,         /* Name of function in C++ (must match Python name) */
            neighbor              /* Argument(s) */
        );
    }
};

void exportNeighborhood(py::module &m)
{
    // Export class NeighborhoodListener to Python
    py::class_<NeighborhoodListener, PyNeighborhoodListener, std::shared_ptr<NeighborhoodListener>>
              (m, "NeighborhoodListener", "A listener for neighborhood events")
        .def(py::init<>())
        .def("neighborAdded",
            &NeighborhoodListener::neighborAdded,
            "Called when a new neighbor is added")
        .def("neighborRemoved",
            &NeighborhoodListener::neighborRemoved,
            "Called when a neighbor is removed")
        .def("gatewayAdded",
            &NeighborhoodListener::gatewayAdded,
            "Called when a new gateway is added")
        ;

    // Export class Neighborhood to Python
    py::class_<Neighborhood, std::shared_ptr<Neighborhood>>(m, "Neighborhood", "The local one-hop neighborhood")
        .def(py::init<NodeId>(),
            py::arg("node_id"))
        .def_readonly("me",
            &Neighborhood::me,
            "Node: this node")
        .def_property_readonly("time_master",
            &Neighborhood::getTimeMaster,
            "Node: the time master")
        .def_property_readonly("neighbors",
            &Neighborhood::getNeighbors,
            "One-hop neighbors")
        .def("__getitem__",
            &Neighborhood::operator[])
        .def("addNeighbor",
            py::overload_cast<NodeId>(&Neighborhood::addNeighbor),
            py::call_guard<py::gil_scoped_release>(),
            "Add a one-hop neighbor")
        .def("addNeighbor",
            py::overload_cast<const std::shared_ptr<Node>&>(&Neighborhood::addNeighbor),
            py::call_guard<py::gil_scoped_release>(),
            "Add a one-hop neighbor")
        .def("removeNeighbor",
            &Neighborhood::removeNeighbor,
            py::call_guard<py::gil_scoped_release>(),
            "Remove a one-hop neighbor")
        .def("addListener",
            &Neighborhood::addListener,
            "Add a listener")
        .def("removeListener",
            &Neighborhood::removeListener,
            "Remove a listener")
        ;
}
