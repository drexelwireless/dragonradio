// Copyright 2018-2023 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/stl.h>

#include "net/Element.hh"
#include "python/PyModules.hh"

void exportElement(py::module &m)
{
    // Export port wrapper classes to Python
    auto net_in_push = py::class_<NetInPush, std::unique_ptr<NetInPush>>(m, "NetInPush");
    auto net_in_pull = py::class_<NetInPull, std::unique_ptr<NetInPull>>(m, "NetInPull");
    auto net_out_pull = py::class_<NetOutPull, std::unique_ptr<NetOutPull>>(m, "NetOutPull");
    auto net_out_push = py::class_<NetOutPush, std::unique_ptr<NetOutPush>>(m, "NetOutPush");

    net_in_push
        .def("__lshift__", [](NetInPush *in, NetOutPush *out) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](NetInPush *in) { if (in->port->isConnected()) { reinterpret_cast<NetOut<Push>*>(in->port->partner())->disconnect(); } })
        ;

    net_in_pull
        .def("__lshift__", [](NetInPull *in, NetOutPull *out) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](NetInPull *in) { in->port->disconnect(); } )
        ;

    net_out_pull
        .def("__rshift__", [](NetOutPull *out, NetInPull *in) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](NetOutPull *out) { if (out->port->isConnected()) { reinterpret_cast<NetIn<Pull>*>(out->port->partner())->disconnect(); } })
        ;

    net_out_push
        .def("__rshift__", [](NetOutPush *out, NetInPush *in) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](NetOutPush *out) { out->port->disconnect(); } )
        ;

    auto radio_in_push = py::class_<RadioInPush, std::unique_ptr<RadioInPush>>(m, "RadioInPush");
    auto radio_in_pull = py::class_<RadioInPull, std::unique_ptr<RadioInPull>>(m, "RadioInPull");
    auto radio_out_pull = py::class_<RadioOutPull, std::unique_ptr<RadioOutPull>>(m, "RadioOutPull");
    auto radio_out_push = py::class_<RadioOutPush, std::unique_ptr<RadioOutPush>>(m, "RadioOutPush");

    radio_in_push
        .def("__lshift__", [](RadioInPush *in, RadioOutPush *out) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](RadioInPush *in) { if (in->port->isConnected()) { reinterpret_cast<RadioOut<Push>*>(in->port->partner())->disconnect(); } })
        ;

    radio_in_pull
        .def("__lshift__", [](RadioInPull *in, RadioOutPull *out) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](RadioInPull *in) { in->port->disconnect(); } )
        ;

    radio_out_pull
        .def("__rshift__", [](RadioOutPull *out, RadioInPull *in) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](RadioOutPull *out) { if (out->port->isConnected()) { reinterpret_cast<RadioIn<Pull>*>(out->port->partner())->disconnect(); } })
        ;

    radio_out_push
        .def("__rshift__", [](RadioOutPush *out, RadioInPush *in) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](RadioOutPush *out) { out->port->disconnect(); } )
        ;
}
