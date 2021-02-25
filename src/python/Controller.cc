// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "llc/Controller.hh"
#include "llc/DummyController.hh"
#include "llc/SmartController.hh"
#include "llc/SmartController/proxies.hh"
#include "python/PyModules.hh"

void exportControllers(py::module &m)
{
    // Export class Controller to Python
    py::class_<Controller, std::shared_ptr<Controller>>(m, "Controller")
        .def_property_readonly("net_in", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->net_in); } )
        .def_property_readonly("net_out", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->net_out); } )
        .def_property_readonly("radio_in", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->radio_in); } )
        .def_property_readonly("radio_out", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->radio_out); } )
        .def_property("net_queue",
            &Controller::getNetQueue,
            &Controller::setNetQueue)
        .def_property("min_channel_bandwidth",
            nullptr,
            &Controller::setMinChannelBandwidth,
            "Minimum channel bandwidth")
        ;

    // Export class DummyController to Python
    py::class_<DummyController, Controller, std::shared_ptr<DummyController>>(m, "DummyController")
        .def(py::init<std::shared_ptr<RadioNet>,
                      size_t>())
        ;

    // Export class SmartController to Python
    py::class_<SmartController, Controller, std::shared_ptr<SmartController>>(m, "SmartController")
        .def(py::init<std::shared_ptr<RadioNet>,
                      size_t,
                      std::shared_ptr<PHY>,
                      double,
                      Seq::uint_type,
                      Seq::uint_type,
                      const std::vector<SmartController::evm_thresh_t>&>())
        .def_readwrite("broadcast_gain",
            &SmartController::broadcast_gain,
            py::return_value_policy::reference_internal)
        .def_readwrite("ack_gain",
            &SmartController::ack_gain,
            py::return_value_policy::reference_internal)
        .def_property("short_per_window",
            &SmartController::getShortPERWindow,
            &SmartController::setShortPERWindow,
            "Time window used to calculate short-term PER")
        .def_property("long_per_window",
            &SmartController::getLongPERWindow,
            &SmartController::setLongPERWindow,
            "Time window used to calculate long-term PE")
        .def_property("long_stats_window",
            &SmartController::getLongStatsWindow,
            &SmartController::setLongStatsWindow,
            "Time window used to calculate long-term statistics, e.g., EVM and RSSI")
        .def_property("mcsidx_broadcast",
            &SmartController::getBroadcastMCSIndex,
            &SmartController::setBroadcastMCSIndex,
            "Broadcast MCS index")
        .def_property("mcsidx_ack",
            &SmartController::getAckMCSIndex,
            &SmartController::setAckMCSIndex,
            "ACK MCS index")
        .def_property("mcsidx_min",
            &SmartController::getMinMCSIndex,
            &SmartController::setMinMCSIndex,
            "Minimum MCS index")
        .def_property("mcsidx_max",
            &SmartController::getMaxMCSIndex,
            &SmartController::setMaxMCSIndex,
            "Maximum MCS index")
        .def_property("mcsidx_init",
            &SmartController::getInitialMCSIndex,
            &SmartController::setInitialMCSIndex,
            "Initial MCS index")
        .def_property("mcsidx_up_per_threshold",
            &SmartController::getUpPERThreshold,
            &SmartController::setUpPERThreshold,
            "PER threshold for increasing modulation level")
        .def_property("mcsidx_down_per_threshold",
            &SmartController::getDownPERThreshold,
            &SmartController::setDownPERThreshold,
            "PER threshold for decreasing modulation level")
        .def_property("mcsidx_alpha",
            &SmartController::getMCSLearningAlpha,
            &SmartController::setMCSLearningAlpha,
            "MCS index learning alpha")
        .def_property("mcsidx_prob_floor",
            &SmartController::getMCSProbFloor,
            &SmartController::setMCSProbFloor,
            "MCS transition probability floor")
        .def_property("ack_delay",
            &SmartController::getACKDelay,
            &SmartController::setACKDelay,
            "ACK delay (sec)")
        .def_property("ack_delay_estimation_window",
            &SmartController::getACKDelayEstimationWindow,
            &SmartController::setACKDelayEstimationWindow,
            "ACK delay estimation window (sec)")
        .def_property("retransmission_delay",
            &SmartController::getRetransmissionDelay,
            &SmartController::setRetransmissionDelay,
            "Retransmission delay (sec)")
        .def_property("min_retransmission_delay",
            &SmartController::getMinRetransmissionDelay,
            &SmartController::setMinRetransmissionDelay,
            "Minimum retransmission delay (sec)")
        .def_property("retransmission_delay_slop",
            &SmartController::getRetransmissionDelaySlop,
            &SmartController::setRetransmissionDelaySlop,
            "Retransmission delay safety factor")
        .def_property("sack_delay",
            &SmartController::getSACKDelay,
            &SmartController::setSACKDelay,
            "SACK delay (sec)")
        .def_property("explicit_nak_window",
            &SmartController::getExplicitNAKWindow,
            &SmartController::setExplicitNAKWindow,
            "Explicit NAK window size")
        .def_property("explicit_nak_window_duration",
            &SmartController::getExplicitNAKWindowDuration,
            &SmartController::setExplicitNAKWindowDuration,
            "Explicit NAK window duration")
        .def_property("selective_ack",
            &SmartController::getSelectiveACK,
            &SmartController::setSelectiveACK,
            "Send selective ACK's?")
        .def_property("selective_ack_feedback_delay",
            &SmartController::getSelectiveACKFeedbackDelay,
            &SmartController::setSelectiveACKFeedbackDelay,
            "Selective ACK feedback delay (sec)")
        .def_property("max_retransmissions",
            &SmartController::getMaxRetransmissions,
            &SmartController::setMaxRetransmissions)
        .def_property("demod_always_ordered",
            &SmartController::getDemodAlwaysOrdered,
            &SmartController::setDemodAlwaysOrdered)
        .def_property("enforce_ordering",
            &SmartController::getEnforceOrdering,
            &SmartController::setEnforceOrdering)
        .def_property("mcu",
            &SmartController::getMCU,
            &SmartController::setMCU,
            "Maximum number of extra control bytes beyond MTU")
        .def_property("move_along",
            &SmartController::getMoveAlong,
            &SmartController::setMoveAlong,
            "Should we always move the send window along even if it's full?")
        .def_property("decrease_retrans_mcsidx",
            &SmartController::getDecreaseRetransMCSIdx,
            &SmartController::setDecreaseRetransMCSIdx,
            "Should we decrease the MCS index of retransmitted packets with a deadline?")
        .def_property_readonly("send",
            [](std::shared_ptr<SmartController> controller) -> std::unique_ptr<SendWindowsProxy>
            {
                return std::make_unique<SendWindowsProxy>(controller);
            },
            "Send windows")
        .def_property_readonly("recv",
            [](std::shared_ptr<SmartController> controller) -> std::unique_ptr<ReceiveWindowsProxy>
            {
                return std::make_unique<ReceiveWindowsProxy>(controller);
            },
            "Receive windows")
        .def("broadcastHello",
            &SmartController::broadcastHello)
        .def("resetMCSTransitionProbabilities",
            &SmartController::resetMCSTransitionProbabilities,
            "Reset all AMC transition probabilties to 1.0")
        ;

    // Export class SendWindowsProxy to Python
    py::class_<SendWindowProxy, std::unique_ptr<SendWindowProxy>>(m, "SendWindow")
        .def_property_readonly("short_per",
            [](SendWindowProxy &proxy) { return proxy.getShortPER(); },
            "Short-term packet error rate (unitless)")
        .def_property_readonly("long_per",
            [](SendWindowProxy &proxy) { return proxy.getLongPER(); },
            "Long-term packet error rate (unitless)")
        .def_property_readonly("long_evm",
            [](SendWindowProxy &proxy) { return proxy.getLongEVM(); },
            "Long-term EVM (dB)")
        .def_property_readonly("long_rssi",
            [](SendWindowProxy &proxy) { return proxy.getLongRSSI(); },
            "Long-term RSSI (dB)")
        ;

    // Export class SendWindowsProxy to Python
    py::class_<SendWindowsProxy, std::unique_ptr<SendWindowsProxy>>(m, "SendWindows")
        .def("__getitem__",
            [](SendWindowsProxy &proxy, NodeId key) -> std::unique_ptr<SendWindowProxy>
            {
                try {
                    return std::make_unique<SendWindowProxy>(proxy[key]);
                } catch (const std::out_of_range&) {
                    throw py::key_error("node '" + std::to_string(key) + "' does not have a send window");
                }
            })
        ;

    // Export class ReceiveWindowProxy to Python
    py::class_<ReceiveWindowProxy, std::unique_ptr<ReceiveWindowProxy>>(m, "ReceiveWindow")
        .def_property_readonly("long_evm",
            [](ReceiveWindowProxy &proxy) { return proxy.getLongEVM(); },
            "Long-term EVM (dB)")
        .def_property_readonly("long_rssi",
            [](ReceiveWindowProxy &proxy) { return proxy.getLongRSSI(); },
            "Long-term RSSI (dB)")
        ;

    // Export class ReceiveWindowsProxy to Python
    py::class_<ReceiveWindowsProxy, std::unique_ptr<ReceiveWindowsProxy>>(m, "ReceiveWindows")
        .def("__getitem__",
            [](ReceiveWindowsProxy &proxy, NodeId key) -> std::unique_ptr<ReceiveWindowProxy>
            {
                try {
                    return std::make_unique<ReceiveWindowProxy>(proxy[key]);
                } catch (const std::out_of_range&) {
                    throw py::key_error("node '" + std::to_string(key) + "' does not have a receive window");
                }
            })
        ;
}
