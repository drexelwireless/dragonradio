// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "logging.hh"
#include "Clock.hh"
#include "Logger.hh"
#include "python/PyModules.hh"

#if !defined(DOXYGEN)
PYBIND11_MAKE_OPAQUE(std::vector<SelfTX>)
#endif /* !defined(DOXYGEN) */

std::shared_ptr<Logger> mkLogger(const std::string& path)
{
    int64_t full_secs = WallClock::now().time_since_epoch().count().get_full();
    auto    dur = WallClock::duration{timerep_t{full_secs, 0.0}};
    auto    log = std::make_shared<Logger>(WallClock::time_point{dur}, MonoClock::time_point{dur});

    log->open(path);
    log->setAttribute("start", (int64_t) full_secs);

    return log;
}

void addLoggerSource(py::class_<Logger, std::shared_ptr<Logger>>& cls, const std::string &name, Logger::Source src)
{
    cls.def_property(name.c_str(),
        [src](std::shared_ptr<Logger> log) { return log->getCollectSource(src); },
        [src](std::shared_ptr<Logger> log, bool collect) { log->setCollectSource(src, collect); });
}

void exportLogger(py::module &m)
{
    // Create enum type EventCategory for logger categories
    py::enum_<EventCategory> event_cat(m, "EventCategory");

    event_cat.def(py::init([](std::string value) -> EventCategory {
            return string2EventCategory(value);
        }));

    py::implicitly_convertible<py::str, EventCategory>();

    for (unsigned i = 0; i < kNumEvents; ++i) {
        EventCategory cat = static_cast<EventCategory>(i);

        event_cat.value(eventCategory2string(cat).c_str(), cat);
    }

    event_cat.export_values();

    // Export log level functions
    m.def("isLogLevelEnabled",
        &isLogLevelEnabled,
        "Return True if log level is enabled");

    m.def("setLogLevel",
        &setLogLevel,
        "Set log level");

    m.def("isPrintLogLevelEnabled",
        &isPrintLogLevelEnabled,
        "Return True if printing log level is enabled");

    m.def("setPrintLogLevel",
        &setPrintLogLevel,
        "Set printing log level");

    // Export class Logger to Python
    py::class_<Logger, std::shared_ptr<Logger>> loggerCls(m, "Logger");

    loggerCls
        .def_property_static("singleton",
            [](py::object) { return logger; },
            [](py::object, std::shared_ptr<Logger> log) { return logger = log; })
        .def(py::init(&mkLogger))
        .def("close", &Logger::close)
        .def("setAttribute", py::overload_cast<const std::string&, const std::string&>(&Logger::setAttribute))
        .def("setAttribute", py::overload_cast<const std::string&, uint8_t>(&Logger::setAttribute))
        .def("setAttribute", py::overload_cast<const std::string&, uint32_t>(&Logger::setAttribute))
        .def("setAttribute", py::overload_cast<const std::string&, double>(&Logger::setAttribute))
        .def("logEvent",
            [](Logger &self, const std::string &msg)
            {
                return self.logEvent(MonoClock::now(), msg);
            },
            "Log an event")
        .def("logSnapshot",
            &Logger::logSnapshot,
            "Log a snapshot")
        ;

    addLoggerSource(loggerCls, "log_slots", Logger::kSlots);
    addLoggerSource(loggerCls, "log_tx_records", Logger::kTXRecords);
    addLoggerSource(loggerCls, "log_recv_packets", Logger::kRecvPackets);
    addLoggerSource(loggerCls, "log_recv_symbols", Logger::kRecvSymbols);
    addLoggerSource(loggerCls, "log_sent_packets", Logger::kSentPackets);
    addLoggerSource(loggerCls, "log_sent_iq", Logger::kSentIQ);
    addLoggerSource(loggerCls, "log_events", Logger::kEvents);
    addLoggerSource(loggerCls, "log_arq_events", Logger::kARQEvents);
}
