#include "net/Net.hh"
#include "net/NetFilter.hh"
#include "net/Noop.hh"
#include "net/Queue.hh"
#include "python/PyModules.hh"

void exportNet(py::module &m)
{
    // Export port wrapper classes to Python
    py::class_<NetInPush, std::unique_ptr<NetInPush>>(m, "NetInPush")
        .def("__lshift__", [](NetInPush *in, NetOutPush *out) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](NetInPush *in) { if (in->port->isConnected()) { reinterpret_cast<NetOut<Push>*>(in->port->partner())->disconnect(); } })
        ;
    py::class_<NetInPull, std::unique_ptr<NetInPull>>(m, "NetInPull")
        .def("__lshift__", [](NetInPull *in, NetOutPull *out) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](NetInPull *in) { in->port->disconnect(); } )
        ;
    py::class_<NetOutPull, std::unique_ptr<NetOutPull>>(m, "NetOutPull")
        .def("__rshift__", [](NetOutPull *out, NetInPull *in) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](NetOutPull *out) { if (out->port->isConnected()) { reinterpret_cast<NetIn<Pull>*>(out->port->partner())->disconnect(); } })
        ;
    py::class_<NetOutPush, std::unique_ptr<NetOutPush>>(m, "NetOutPush")
        .def("__rshift__", [](NetOutPush *out, NetInPush *in) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](NetOutPush *out) { out->port->disconnect(); } )
        ;

    py::class_<RadioInPush, std::unique_ptr<RadioInPush>>(m, "RadioInPush")
        .def("__lshift__", [](RadioInPush *in, RadioOutPush *out) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](RadioInPush *in) { if (in->port->isConnected()) { reinterpret_cast<RadioOut<Push>*>(in->port->partner())->disconnect(); } })
        ;
    py::class_<RadioInPull, std::unique_ptr<RadioInPull>>(m, "RadioInPull")
        .def("__lshift__", [](RadioInPull *in, RadioOutPull *out) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](RadioInPull *in) { in->port->disconnect(); } )
        ;
    py::class_<RadioOutPull, std::unique_ptr<RadioOutPull>>(m, "RadioOutPull")
        .def("__rshift__", [](RadioOutPull *out, RadioInPull *in) { in->port->connect(out->element, out->port); } )
        .def("disconnect", [](RadioOutPull *out) { if (out->port->isConnected()) { reinterpret_cast<RadioIn<Pull>*>(out->port->partner())->disconnect(); } })
        ;
    py::class_<RadioOutPush, std::unique_ptr<RadioOutPush>>(m, "RadioOutPush")
        .def("__rshift__", [](RadioOutPush *out, RadioInPush *in) { out->port->connect(in->element, in->port); } )
        .def("disconnect", [](RadioOutPush *out) { out->port->disconnect(); } )
        ;

    // Export class NetQueue to Python
    py::class_<NetQueue, std::shared_ptr<NetQueue>>(m, "NetQueue")
        .def_property_readonly("push", [](std::shared_ptr<NetQueue> element) { return exposePort(element, &element->in); } )
        .def_property_readonly("pop", [](std::shared_ptr<NetQueue> element) { return exposePort(element, &element->out); } )
        ;

    // Export class NetFIFO to Python
    py::class_<NetFIFO, NetQueue, std::shared_ptr<NetFIFO>>(m, "NetFIFO")
        .def(py::init())
        ;

    // Export class NetLIFO to Python
    py::class_<NetLIFO, NetQueue, std::shared_ptr<NetLIFO>>(m, "NetLIFO")
        .def(py::init())
        ;

    // Export class TunTap to Python
    py::class_<TunTap, std::shared_ptr<TunTap>>(m, "TunTap")
        .def(py::init<const std::string&,
                      bool,
                      size_t,
                      uint8_t>())
        .def_property_readonly("mtu", &TunTap::getMTU)
        .def_property_readonly("source", [](std::shared_ptr<TunTap> element) { return exposePort(element, &element->source); } )
        .def_property_readonly("sink", [](std::shared_ptr<TunTap> element) { return exposePort(element, &element->sink); } )
        ;

    // Export class NetProcessor to Python
    py::class_<NetProcessor, std::shared_ptr<NetProcessor>>(m, "NetProcessor")
        .def_property_readonly("input", [](std::shared_ptr<NetProcessor> element) { return exposePort(element, &element->in); } )
        .def_property_readonly("output", [](std::shared_ptr<NetProcessor> element) { return exposePort(element, &element->out); } )
        ;

    // Export class RadioProcessor to Python
    py::class_<RadioProcessor, std::shared_ptr<RadioProcessor>>(m, "RadioProcessor")
        .def_property_readonly("input", [](std::shared_ptr<RadioProcessor> element) { return exposePort(element, &element->in); } )
        .def_property_readonly("output", [](std::shared_ptr<RadioProcessor> element) { return exposePort(element, &element->out); } )
        ;

    // Export class NetFilter to Python
    py::class_<NetFilter, NetProcessor, std::shared_ptr<NetFilter>>(m, "NetFilter")
        .def(py::init<std::shared_ptr<Net>>())
        ;

    // Export class NetNoop to Python
    py::class_<NetNoop, NetProcessor, std::shared_ptr<NetNoop>>(m, "NetNoop")
        .def(py::init<>())
        ;

    // Export class NetNoop to Python
    py::class_<RadioNoop, RadioProcessor, std::shared_ptr<RadioNoop>>(m, "RadioNoop")
        .def(py::init<>())
        ;
}
