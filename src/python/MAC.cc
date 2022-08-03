// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <time.h>

#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "mac/FDMA.hh"
#include "mac/SlottedALOHA.hh"
#include "mac/SlottedMAC.hh"
#include "mac/TDMA.hh"
#include "python/PyModules.hh"

void exportMACs(py::module &m)
{
    // Export class Schedule to Python
    py::class_<Schedule, std::unique_ptr<Schedule>>(m, "Schedule")
        .def(py::init<const Schedule::sched_type&,
                      std::chrono::duration<double>,
                      std::chrono::duration<double>,
                      bool>(),
            py::arg("schedule"),
            py::arg("slot_size") = 0s,
            py::arg("guard_size") = 0s,
            py::arg("superslots") = false)
        .def_property("slot_size",
            [](Schedule &self) -> double
            {
                return self.getSlotSize().count();
            },
            &Schedule::setSlotSize,
            "float: Slot size (sec)")
        .def_property("guard_size",
            [](Schedule &self) -> double
            {
                return self.getGuardSize().count();
            },
            &Schedule::setGuardSize,
            "float: Guard size (sec)")
        .def_property_readonly("frame_size",
            [](Schedule &self) -> double
            {
                return self.getFrameSize().count();
            },
            "float: Frame size (sec)")
        .def_property("superslots",
            &Schedule::getSuperslots,
            &Schedule::setSuperslots,
            "bool: Allow superslots")
        .def_property_readonly("nchannels",
            &Schedule::nchannels,
            "int: Number of channels")
        .def_property_readonly("nslots",
            &Schedule::nslots,
            "int: Number of slots")
        ;

    // Export class MAC to Python
    py::class_<MAC, std::shared_ptr<MAC>>(m, "MAC")
        .def("stop",
            &MAC::stop,
            "Tell MAC to stop processing packets.")
        .def("rateChange",
            &MAC::rateChange,
            "Notify the MAC of a TX/RX rate change")
        .def_property("schedule",
            &MAC::getSchedule,
            [](MAC &self, py::object obj)
            {
                try {
                    return self.setSchedule(obj.cast<const Schedule::sched_type>());
                } catch (py::cast_error &) {
                    return self.setSchedule(obj.cast<const Schedule>());
                }
            },
            "Schedule: MAC schedule specifying on which channels this node may transmit in each schedule slot.")
        .def("getLoad",
            &MAC::getLoad,
            "Get current load")
        .def("popLoad",
            &MAC::popLoad,
            "Get current load and reset load counters")
        ;

    // Export class MAC::Load to Python
    using Load = MAC::Load;

    py::class_<Load, std::shared_ptr<Load>>(m, "Load")
        .def_property_readonly("start",
            [](Load &self) -> double
            {
                return self.start.time_since_epoch().count();
            },
            "float: Start of load measurement period (sec)")
        .def_property_readonly("end",
            [](Load &self) -> double
            {
                return self.end.time_since_epoch().count();
            },
            "float: End of load measurement period (sec)")
        .def_property_readonly("period",
            [](Load &self) -> double
            {
                return (self.end - self.start).count();
            },
            "float: Measurement period (sec)")
        .def_readwrite("nsamples",
            &SlottedMAC::Load::nsamples,
            "int: Load per channel measured in number of samples")
        ;

    // Export class FDMA to Python
    py::class_<FDMA, MAC, std::shared_ptr<FDMA>>(m, "FDMA")
        .def(py::init<std::shared_ptr<Radio>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<Synthesizer>,
                      double>(),
            py::arg("radio"),
            py::arg("controller"),
            py::arg("snapshot_collector"),
            py::arg("channelizer"),
            py::arg("synthesizer"),
            py::arg("premodulation"))
        .def_property("accurate_tx_timestamps",
            &FDMA::getAccurateTXTimestamps,
            &FDMA::setAccurateTXTimestamps,
            "bool: Increase timestamp accuracy at a potential cost to performance")
        ;

    // Export class SlottedMAC to Python
    py::class_<SlottedMAC, MAC, std::shared_ptr<SlottedMAC>>(m, "SlottedMAC")
        .def_property("slot_send_lead_time",
            &SlottedMAC::getSlotSendLeadTime,
            &SlottedMAC::setSlotSendLeadTime,
            "float: Slot send lead time (sec)")
        ;

    // Export class TDMA to Python
    py::class_<TDMA, MAC, std::shared_ptr<TDMA>>(m, "TDMA")
        .def(py::init<std::shared_ptr<Radio>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<Synthesizer>,
                      double>(),
            py::arg("radio"),
            py::arg("controller"),
            py::arg("snapshot_collector"),
            py::arg("channelizer"),
            py::arg("synthesizer"),
            py::arg("rx_period"))
        ;

    // Export class SlottedALOHA to Python
    py::class_<SlottedALOHA, MAC, std::shared_ptr<SlottedALOHA>>(m, "SlottedALOHA")
        .def(py::init<std::shared_ptr<Radio>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<Synthesizer>,
                      double,
                      double>(),
            py::arg("radio"),
            py::arg("controller"),
            py::arg("snapshot_collector"),
            py::arg("channelizer"),
            py::arg("synthesizer"),
            py::arg("rx_period"),
            py::arg("probability"))
        .def_property("slotidx",
            &SlottedALOHA::getSlotIndex,
            &SlottedALOHA::setSlotIndex,
            "int: Slot index to transmit in")
        .def_property("p",
            &SlottedALOHA::getTXProb,
            &SlottedALOHA::setTXProb,
            "float: Probability of transmission in a given slot")
        ;
}
