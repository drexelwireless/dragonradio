// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "Clock.hh"
#include "Logger.hh"
#include "python/PyModules.hh"

#if !defined(DOXYGEN)
PYBIND11_MAKE_OPAQUE(std::vector<SelfTX>)
#endif /* !defined(DOXYGEN) */

std::shared_ptr<Logger> mkLogger(const std::string& path)
{
    Clock::time_point t_start = Clock::time_point(Clock::now().get_full_secs());
    auto              log = std::make_shared<Logger>(t_start);

    log->open(path);
    log->setAttribute("start", (uint32_t) t_start.get_full_secs());

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
                return self.logEvent(Clock::now(), msg);
            },
            "Log an event")
        .def("logSnapshot",
            &Logger::logSnapshot,
            "Log a snapshot")
        ;

    addLoggerSource(loggerCls, "log_slots", Logger::kSlots);
    addLoggerSource(loggerCls, "log_recv_packets", Logger::kRecvPackets);
    addLoggerSource(loggerCls, "log_recv_symbols", Logger::kRecvSymbols);
    addLoggerSource(loggerCls, "log_sent_packets", Logger::kSentPackets);
    addLoggerSource(loggerCls, "log_sent_iq", Logger::kSentIQ);
    addLoggerSource(loggerCls, "log_events", Logger::kEvents);
}
