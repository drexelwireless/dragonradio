// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "RadioNet.hh"
#include "net/MandateQueue.hh"
#include "net/NetFilter.hh"
#include "net/Noop.hh"
#include "net/PacketCompressor.hh"
#include "net/REDQueue.hh"
#include "net/SimpleQueue.hh"
#include "net/SizedQueue.hh"
#include "net/Queue.hh"
#include "python/PyModules.hh"
#include "util/net.hh"

#if !defined(DOXYGEN)
PYBIND11_MAKE_OPAQUE(MandateMap)
#endif /* !defined(DOXYGEN) */

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
    py::class_<NetQueue, std::shared_ptr<NetQueue>>(m, "Queue")
        .def_property("transmission_delay",
            &NetQueue::getTransmissionDelay,
            &NetQueue::setTransmissionDelay,
            "Transmission delay (sec)")
        .def_property_readonly("push", [](std::shared_ptr<NetQueue> element) { return exposePort(element, &element->in); } )
        .def_property_readonly("pop", [](std::shared_ptr<NetQueue> element) { return exposePort(element, &element->out); } )
        ;

    // Export class SimpleNetQueue to Python
    auto simple_queue_class = py::class_<SimpleNetQueue, NetQueue, std::shared_ptr<SimpleNetQueue>>(m, "SimpleQueue")
        .def(py::init<SimpleNetQueue::QueueType>())
        ;

    py::enum_<SimpleNetQueue::QueueType>(simple_queue_class, "QueueType")
        .value("FIFO", SimpleNetQueue::FIFO)
        .value("LIFO", SimpleNetQueue::LIFO)
        .export_values();

    // Export class SizedNetQueue to Python
    py::class_<SizedNetQueue, NetQueue, std::shared_ptr<SizedNetQueue>>(m, "SizedQueue")
        .def_property("hi_priority_flows",
            &SizedNetQueue::getHiPriorityFlows,
            &SizedNetQueue::setHiPriorityFlows,
            "High-priority flows?")
        ;

    // Export class REDNetQueue to Python
    py::class_<REDNetQueue, SizedNetQueue, std::shared_ptr<REDNetQueue>>(m, "REDQueue")
        .def(py::init<bool,
                      size_t,
                      size_t,
                      double,
                      double>())
        .def_property("gentle",
            &REDNetQueue::getGentle,
            &REDNetQueue::setGentle,
            "Be gentle?")
        .def_property("min_thresh",
            &REDNetQueue::getMinThresh,
            &REDNetQueue::setMinThresh,
            "Minimum threshold (bytes)")
        .def_property("max_thresh",
            &REDNetQueue::getMaxThresh,
            &REDNetQueue::setMaxThresh,
            "Maximum threshold (bytes)")
        .def_property("max_p",
            &REDNetQueue::getMaxP,
            &REDNetQueue::setMaxP,
            "Maximum packet drop probability")
        .def_property("w_q",
            &REDNetQueue::getQueueWeight,
            &REDNetQueue::setQueueWeight,
            "Queue weight (for EWMA)")
        ;

    // Export class MandateNetQueue to Python
    auto mandate_queue_class = py::class_<MandateNetQueue, NetQueue, std::shared_ptr<MandateNetQueue>>(m, "MandateQueue")
        .def(py::init<>())
        .def_property("bonus_phase",
            &MandateNetQueue::getBonusPhase,
            &MandateNetQueue::setBonusPhase,
            "Flag indicating whether or not to have a bonus phase")
        .def("getFlowQueueType",
            &MandateNetQueue::getFlowQueueType,
            "Get flow queue's type")
        .def("setFlowQueueType",
            &MandateNetQueue::setFlowQueueType,
            "Set flow queue's type")
        .def("getFlowQueuePriority",
            &MandateNetQueue::getFlowQueuePriority,
            "Get flow queue's priority")
        .def("setFlowQueuePriority",
            &MandateNetQueue::setFlowQueuePriority,
            "Set flow queue's priority")
        .def_property("mandates",
            &MandateNetQueue::getMandates,
            &MandateNetQueue::setMandates,
            "Mandates")
        .def_property_readonly("queue_priorities",
            &MandateNetQueue::getQueuePriorities,
            "Queue priorities")
        ;

    py::enum_<MandateNetQueue::QueueType>(mandate_queue_class, "QueueType")
        .value("FIFO", MandateNetQueue::FIFO)
        .value("LIFO", MandateNetQueue::LIFO)
        .export_values();

    // Export class TunTap to Python
    py::class_<TunTap, std::shared_ptr<TunTap>>(m, "TunTap")
        .def(py::init<const std::string&,
                      const std::string&,
                      const std::string&,
                      const std::string&,
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
        .def(py::init<std::shared_ptr<RadioNet>,
                      in_addr_t,
                      in_addr_t,
                      in_addr_t,
                      in_addr_t,
                      in_addr_t,
                      in_addr_t>())
        ;

    // Export class NetNoop to Python
    py::class_<NetNoop, NetProcessor, std::shared_ptr<NetNoop>>(m, "NetNoop")
        .def(py::init<>())
        ;

    // Export class RadioNoop to Python
    py::class_<RadioNoop, RadioProcessor, std::shared_ptr<RadioNoop>>(m, "RadioNoop")
        .def(py::init<>())
        ;


    // Export class Compress to Python
    py::class_<PacketCompressor, std::shared_ptr<PacketCompressor>>(m, "PacketCompressor")
        .def(py::init<bool,
                      in_addr_t,
                      in_addr_t,
                      in_addr_t,
                      in_addr_t>())
        .def_property("enabled",
            &PacketCompressor::getEnabled,
            &PacketCompressor::setEnabled,
            "Is packet compression enabled?")
        .def_property_readonly("net_in",
            [](std::shared_ptr<PacketCompressor> element) { return exposePort(element, &element->net_in); },
            "Network packet input port")
        .def_property_readonly("net_out",
            [](std::shared_ptr<PacketCompressor> element) { return exposePort(element, &element->net_out); },
            "Network packet output port")
        .def_property_readonly("radio_in",
            [](std::shared_ptr<PacketCompressor> element) { return exposePort(element, &element->radio_in); },
            "Radio packet input port")
        .def_property_readonly("radio_out",
            [](std::shared_ptr<PacketCompressor> element) { return exposePort(element, &element->radio_out); },
            "Radio packet output port")
        ;
}

void exportNetUtil(py::module &m)
{
    m.def("addStaticARPEntry",
        &addStaticARPEntry,
        "Add a static ARP table entry")
     .def("deleteARPEntry",
        &deleteARPEntry,
        "Delete an ARP table entry")
     .def("addRoute",
        &addRoute,
        "Add an IP route")
     .def("deleteRoute",
        &deleteRoute,
        "Delete an IP route");
}
