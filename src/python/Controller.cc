#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "mac/Controller.hh"
#include "mac/DummyController.hh"
#include "mac/SmartController.hh"
#include "python/PyModules.hh"

void exportControllers(py::module &m)
{
    // Export class Controller to Python
    py::class_<Controller, std::shared_ptr<Controller>>(m, "Controller")
        .def_property_readonly("net_in", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->net_in); } )
        .def_property_readonly("net_out", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->net_out); } )
        .def_property_readonly("radio_in", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->radio_in); } )
        .def_property_readonly("radio_out", [](std::shared_ptr<Controller> element) { return exposePort(element, &element->radio_out); } )
        ;

    // Export class DummyController to Python
    py::class_<DummyController, Controller, std::shared_ptr<DummyController>>(m, "DummyController")
        .def(py::init<std::shared_ptr<Net>,
                      const std::vector<TXParams>&>())
        ;

    // Export class SmartController to Python
    py::class_<SmartController, Controller, std::shared_ptr<SmartController>>(m, "SmartController")
        .def(py::init<std::shared_ptr<Net>,
                      std::shared_ptr<PHY>,
                      double,
                      Seq::uint_type,
                      Seq::uint_type,
                      const std::vector<TXParams>&,
                      const TXParams&,
                      unsigned,
                      double,
                      double,
                      double,
                      double>())
        .def_property("net_queue",
            &SmartController::getNetQueue,
            &SmartController::setNetQueue)
        .def_property("mac",
            &SmartController::getMAC,
            &SmartController::setMAC)
        .def_property_readonly("broadcast_tx_params",
            &SmartController::getBroadcastTXParams,
            "Broadcast TX parameters")
        .def_readwrite("broadcast_gain",
            &SmartController::broadcast_gain,
            py::return_value_policy::reference_internal)
        .def_readwrite("ack_gain",
            &SmartController::ack_gain,
            py::return_value_policy::reference_internal)
        .def_property("samples_per_slot",
            &SmartController::getSamplesPerSlot,
            &SmartController::setSamplesPerSlot,
            "Number of samples in a transmission slot")
        .def_property_readonly("tx_params",
            &SmartController::getTXParams,
            "TX parameters")
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
        .def_property_readonly("echoed_timestamps",
            &SmartController::getEchoedTimestamps,
            "Our timestamps echoed by the time master")
        .def_property_readonly("send",
            [](std::shared_ptr<SmartController> controller) -> std::unique_ptr<SendWindowsProxy>
            {
                return std::make_unique<SendWindowsProxy>(controller);
            },
            "Send windows")
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
}
