// Copyright 2018-2021 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "llc/Controller.hh"
#include "llc/DummyController.hh"
#include "llc/SmartController.hh"
#include "python/PyModules.hh"

/** @brief A proxy object for SmartController's timestamps */
class TimestampsProxy
{
public:
    TimestampsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    TimestampsProxy() = delete;
    ~TimestampsProxy() = default;

    Timestamps::timestamps_map operator [](NodeId node)
    {
        if (controller_->timestampsContains(node))
            return controller_->getTimestamps(node);
        else
            throw std::out_of_range("No timestamps");
    }

    bool contains(NodeId node_id) const
    {
        return controller_->timestampsContains(node_id);
    }

    std::set<NodeId> keys(void) const
    {
        return controller_->getTimestampsNodes();
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

/** @brief A proxy object for a SmartController send window */
class SendWindowProxy
{
public:
    SendWindowProxy(std::shared_ptr<SmartController> controller,
                    NodeId node_id)
      : controller_(controller)
      , node_id_(node_id)
    {
    }

    std::optional<double> getShortPER(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->short_per.value();
    }

    std::optional<double> getLongPER(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->long_per.value();
    }

    std::optional<double> getShortEVM(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->short_evm;
    }

    std::optional<double> getLongEVM(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->long_evm;
    }

    std::optional<double> getShortRSSI(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->short_rssi;
    }

    std::optional<double> getLongRSSI(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->long_rssi;
    }

    size_t getMCSIdx(void)
    {
        SendWindowGuard sendw(*controller_, node_id_);

        return sendw->mcsidx;
    }

private:
    /** @brief This send window's SmartController */
    std::shared_ptr<SmartController> controller_;

    /** @brief This send window's node ID */
    const NodeId node_id_;
};

/** @brief A proxy object for SmartController's send windows */
class SendWindowsProxy
{
public:
    SendWindowsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    SendWindowsProxy() = delete;
    ~SendWindowsProxy() = default;

    SendWindowProxy operator [](NodeId node)
    {
        if (controller_->sendWindowContains(node))
            return SendWindowProxy(controller_, node);
        else
            throw std::out_of_range("No send window");
    }

    bool contains(NodeId node_id) const
    {
        return controller_->sendWindowContains(node_id);
    }

    std::set<NodeId> keys(void) const
    {
        return controller_->getSendWindowNodes();
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

/** @brief A proxy object for a SmartController receive window */
class ReceiveWindowProxy
{
public:
    ReceiveWindowProxy(std::shared_ptr<SmartController> controller,
                       NodeId node_id)
      : controller_(controller)
      , node_id_(node_id)
    {
    }

    std::optional<double> getShortEVM(void)
    {
        RecvWindowGuard recvw(*controller_, node_id_);

        return recvw->short_evm.value();
    }

    std::optional<double> getLongEVM(void)
    {
        RecvWindowGuard recvw(*controller_, node_id_);

        return recvw->long_evm.value();
    }

    std::optional<double> getShortRSSI(void)
    {
        RecvWindowGuard recvw(*controller_, node_id_);

        return recvw->short_rssi.value();
    }

    std::optional<double> getLongRSSI(void)
    {
        RecvWindowGuard recvw(*controller_, node_id_);

        return recvw->long_rssi.value();
    }

private:
    /** @brief This send window's SmartController */
    std::shared_ptr<SmartController> controller_;

    /** @brief This send window's node ID */
    const NodeId node_id_;
};

/** @brief A proxy object for SmartController's receive windows */
class ReceiveWindowsProxy
{
public:
    ReceiveWindowsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    ReceiveWindowsProxy() = delete;
    ~ReceiveWindowsProxy() = default;

    ReceiveWindowProxy operator [](NodeId node)
    {
        if (controller_->recvWindowContains(node))
            return ReceiveWindowProxy(controller_, node);
        else
            throw std::out_of_range("No receive window");
    }

    bool contains(NodeId node_id) const
    {
        return controller_->recvWindowContains(node_id);
    }

    std::set<NodeId> keys(void) const
    {
        return controller_->getRecvWindowNodes();
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

void exportControllers(py::module &m)
{

    // Export class ControllerNetLink to Python
    py::class_<ControllerNetLink, std::shared_ptr<ControllerNetLink>>(m, "ControllerNetLink")
        .def_property("transmission_delay",
            &ControllerNetLink::getTransmissionDelay,
            &ControllerNetLink::setTransmissionDelay,
            "Transmission delay (sec)")
        ;

    // Export class Controller to Python
    py::class_<Controller, std::shared_ptr<Controller>>(m, "Controller")
        .def_property_readonly("net_in", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->net_in); } )
        .def_property_readonly("net_out", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->net_out); } )
        .def_property_readonly("radio_in", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->radio_in); } )
        .def_property_readonly("radio_out", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->radio_out); } )
        .def_property("net_link",
            &Controller::getNetLink,
            &Controller::setNetLink)
        .def_property("channels",
            nullptr,
            &Controller::setChannels)
        .def("setEmcon",
            &Controller::setEmcon,
            "Set whether or not a node can transmit")
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
        .def_property("short_stats_window",
            &SmartController::getShortStatsWindow,
            &SmartController::setShortStatsWindow,
            "Time window used to calculate long-term statistics, e.g., EVM and RSSI")
        .def_property("long_stats_window",
            &SmartController::getLongStatsWindow,
            &SmartController::setLongStatsWindow,
            "Time window used to calculate long-term statistics, e.g., EVM and RSSI")
        .def_property("mcs_fast_adjustment_period",
            &SmartController::getMCSFastAdjustmentPeriod,
            &SmartController::setMCSFastAdjustmentPeriod,
            "MCS fast adjustment period after environmnet discontinuity")
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
        .def_property("unreachable_timeout",
            &SmartController::getUnreachableTimeout,
            &SmartController::setUnreachableTimeout,
            "Threshold for marking node unreachable (sec)")
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
        .def_property("max_sacks",
            &SmartController::getMaxSACKs,
            &SmartController::setMaxSACKs,
            "Maximum number of SACKs in a packet")
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
        .def_property_readonly("timestamps",
            [](std::shared_ptr<SmartController> controller) -> std::unique_ptr<TimestampsProxy>
            {
                return std::make_unique<TimestampsProxy>(controller);
            },
            "Timestamps")
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
        .def("environmentDiscontinuity",
            &SmartController::environmentDiscontinuity,
            "Inform the controller that an environmental discontinuity has ocurred")
        ;

    // Export class TimestampsProxy to Python
    py::class_<TimestampsProxy, std::unique_ptr<TimestampsProxy>>(m, "Timestamps")
        .def("__getitem__",
            &TimestampsProxy::operator [])
        .def("__contains__",
            &TimestampsProxy::contains)
        .def("keys",
            &TimestampsProxy::keys)
        ;

    // Export class SendWindowsProxy to Python
    py::class_<SendWindowProxy, std::unique_ptr<SendWindowProxy>>(m, "SendWindow")
        .def_property_readonly("short_per",
            &SendWindowProxy::getShortPER,
            "Short-term packet error rate (unitless)")
        .def_property_readonly("long_per",
            &SendWindowProxy::getLongPER,
            "Long-term packet error rate (unitless)")
        .def_property_readonly("short_evm",
            &SendWindowProxy::getShortEVM,
            "Short-term EVM (dB)")
        .def_property_readonly("long_evm",
            &SendWindowProxy::getLongEVM,
            "Long-term EVM (dB)")
        .def_property_readonly("short_rssi",
            &SendWindowProxy::getShortRSSI,
            "Short-term RSSI (dB)")
        .def_property_readonly("long_rssi",
            &SendWindowProxy::getLongRSSI,
            "Long-term RSSI (dB)")
        .def_property_readonly("mcsidx",
            &SendWindowProxy::getMCSIdx,
            "MCS index")
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
        .def("__contains__",
            &SendWindowsProxy::contains)
        .def("keys",
            &SendWindowsProxy::keys)
        ;

    // Export class ReceiveWindowProxy to Python
    py::class_<ReceiveWindowProxy, std::unique_ptr<ReceiveWindowProxy>>(m, "ReceiveWindow")
        .def_property_readonly("short_evm",
            &ReceiveWindowProxy::getShortEVM,
            "Short-term EVM (dB)")
        .def_property_readonly("long_evm",
            &ReceiveWindowProxy::getLongEVM,
            "Long-term EVM (dB)")
        .def_property_readonly("short_rssi",
            &ReceiveWindowProxy::getShortRSSI,
            "Short-term RSSI (dB)")
        .def_property_readonly("long_rssi",
            &ReceiveWindowProxy::getLongRSSI,
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
        .def("__contains__",
            &ReceiveWindowsProxy::contains)
        .def("keys",
            &ReceiveWindowsProxy::keys)
        ;
}
