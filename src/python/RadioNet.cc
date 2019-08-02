#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "net/Net.hh"
#include "python/PyModules.hh"

void exportRadioNet(py::module &m)
{
    // Export class TXParams to Python
    py::class_<TXParams, std::shared_ptr<TXParams>>(m, "TXParams")
        .def(py::init<MCS>())
        .def_readonly("mcs",
            &TXParams::mcs,
            "Modulation and coding scheme")
        .def_property("g_0dBFS",
            &TXParams::getSoftTXGain,
            &TXParams::setSoftTXGain,
            "Soft TX gain (multiplicative factor)")
        .def_property("soft_tx_gain_0dBFS",
            &TXParams::getSoftTXGain0dBFS,
            &TXParams::setSoftTXGain0dBFS,
            "Soft TX gain (dBFS)")
        .def_property("auto_soft_tx_gain_clip_frac",
            &TXParams::getAutoSoftTXGainClipFrac,
            &TXParams::setAutoSoftTXGainClipFrac,
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
        .def_property_readonly("tx_params",
            [](Node &node) -> std::optional<TXParams> {
                if (node.tx_params)
                    return *node.tx_params;
                else
                    return std::nullopt;
            },
            "TX parameters")
        .def_readwrite("ack_delay",
            &Node::ack_delay,
            "ACK delay (in seconds)")
        .def_readwrite("retransmission_delay",
            &Node::retransmission_delay,
            "Packet retransmission delay (in seconds)")
        .def_property_readonly("timestamps",
            [](Node &node) {
                std::lock_guard<std::mutex> lock(node.timestamps_mutex);

                return node.timestamps;
            },
            "Timestamps received from this node")
        ;

    // Export class Net to Python
    py::class_<Net, std::shared_ptr<Net>>(m, "Net")
        .def(py::init<std::shared_ptr<TunTap>,
                      NodeId>())
        .def_property_readonly("my_node_id",
            &Net::getMyNodeId)
        .def_property_readonly("nodes",
            &Net::getNodes,
            "Nodes in the network")
        .def_property_readonly("time_master",
            &Net::getTimeMaster)
        .def("addNode",
            [](Net &net, NodeId id) { return net.getNode(id); },
            py::return_value_policy::reference_internal)
        ;
}
