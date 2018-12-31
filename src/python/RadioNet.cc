#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "net/Net.hh"
#include "python/PyModules.hh"

void exportRadioNet(py::module &m)
{
    // Export class TXParams to Python
    py::class_<TXParams, std::shared_ptr<TXParams>>(m, "TXParams")
        .def(py::init<>())
        .def(py::init<MCS>())
        .def_readwrite("mcs",
            &TXParams::mcs,
            "Modulation and coding scheme")
        .def_readwrite("g_0dBFS",
            &TXParams::g_0dBFS,
            "Soft TX gain (multiplicative factor)")
        .def_property("soft_tx_gain_0dBFS",
            &TXParams::getSoftTXGain0dBFS,
            &TXParams::setSoftTXGain0dBFS,
            "Soft TX gain (dBFS)")
        .def_readwrite("auto_soft_tx_gain_clip_frac",
            &TXParams::auto_soft_tx_gain_clip_frac,
            "Clipping threshold for automatic TX soft gain")
        .def("recalc0dBFSEstimate",
            &TXParams::recalc0dBFSEstimate,
            "Reset the 0dBFS estimate")
        ;

    py::bind_vector<std::vector<TXParams>>(m, "TXParamsVector");

    // Export class Node to Python
    py::class_<Node, std::shared_ptr<Node>>(m, "Node")
        .def_readonly("id",
            &Node::id,
            "Node ID")
        .def_readwrite("is_gateway",
            &Node::is_gateway,
            "Flag indicating whether or not this node is the gateway")
        // Make a copy of the Node's TXParams because it shouldn't be modified
        // directly since it is NOT owned by the node.
        .def_property_readonly("tx_params",
            [](Node &node) { return *node.tx_params; },
            "TX parameters",
            py::return_value_policy::copy)
        .def_readwrite("g",
            &Node::g,
            "Soft TX gain (multiplicative)")
        .def_property("soft_tx_gain",
            &Node::getSoftTXGain,
            &Node::setSoftTXGain,
            "Soft TX gain (dBFS)")
        .def_readwrite("ack_delay",
            &Node::ack_delay,
            "ACK delay (in seconds)")
        .def_readwrite("retransmission_delay",
            &Node::retransmission_delay,
            "Packet retransmission delay (in seconds)")
        .def_property_readonly("short_per",
            [](Node &node) { return node.short_per.getValue(); },
            "Short-term packet error rate (unitless)")
        .def_property_readonly("long_per",
            [](Node &node) { return node.long_per.getValue(); },
            "Long-term packet error rate (unitless)")
        .def_readonly("timestamps",
            &Node::timestamps,
            "Timestamps received from this node")
        ;

    // Export class Net to Python
    py::class_<Net, std::shared_ptr<Net>>(m, "Net")
        .def(py::init<std::shared_ptr<TunTap>,
                      NodeId>())
        .def("__getitem__",
            [](Net &net, NodeId key) -> Node&
            {
                try {
                    return net[key];
                } catch (const std::out_of_range&) {
                    throw py::key_error("key '" + std::to_string(key) + "' does not exist");
                }
            },
            py::return_value_policy::reference_internal)
        .def("__len__",
            &Net::size)
        .def("__iter__",
            [](Net &net)
            {
                return py::make_key_iterator(net.begin(), net.end());
            },
            py::keep_alive<0, 1>())
        .def("keys",
            [](Net &net)
            {
                return py::make_key_iterator(net.begin(), net.end());
            },
            py::keep_alive<0, 1>())
        .def("items",
            [](Net &net)
            {
                return py::make_iterator(net.begin(), net.end());
            },
            py::keep_alive<0, 1>(),
            py::return_value_policy::reference_internal)
        .def_property_readonly("my_node_id",
            &Net::getMyNodeId)
        .def_property_readonly("time_master",
            &Net::getTimeMaster)
        .def_readwrite("tx_params",
            &Net::tx_params,
            "TX parameters")
        .def("addNode",
            &Net::addNode,
            py::return_value_policy::reference_internal)
        ;
}
