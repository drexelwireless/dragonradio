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
            py::overload_cast<const Schedule::sched_type &>(&SlottedMAC::setSchedule),
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
                      std::shared_ptr<ChannelSynthesizer>,
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
        .def_property("timed_tx_delay",
            &FDMA::getTimedTXDelay,
            &FDMA::setTimedTXDelay,
            "float: Delay for timed TX (sec)")
        ;

    // Export class SlottedMAC to Python
    py::class_<SlottedMAC, MAC, std::shared_ptr<SlottedMAC>>(m, "SlottedMAC")
        .def_property("slot_size",
            &SlottedMAC::getSlotSize,
            &SlottedMAC::setSlotSize,
            "float: Slot size (sec)")
        .def_property("guard_size",
            &SlottedMAC::getGuardSize,
            &SlottedMAC::setGuardSize,
            "float: Guard size (sec)")
        .def_property("slot_send_lead_time",
            &SlottedMAC::getSlotSendLeadTime,
            &SlottedMAC::setSlotSendLeadTime,
            "float: Slot send lead time (sec)")
        ;

    // Export class TDMA to Python
    py::class_<TDMA, SlottedMAC, std::shared_ptr<TDMA>>(m, "TDMA")
        .def(py::init<std::shared_ptr<Radio>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<SlotSynthesizer>,
                      double,
                      double,
                      double,
                      size_t>(),
            py::arg("radio"),
            py::arg("controller"),
            py::arg("snapshot_collector"),
            py::arg("channelizer"),
            py::arg("synthesizer"),
            py::arg("slot_size"),
            py::arg("guard_size"),
            py::arg("slot_send_lead_time"),
            py::arg("nslots"))
        .def_property_readonly("nslots",
            &TDMA::getNSlots,
            "int: The number of TDMA slots.")
        ;

    // Export class SlottedALOHA to Python
    py::class_<SlottedALOHA, SlottedMAC, std::shared_ptr<SlottedALOHA>>(m, "SlottedALOHA")
        .def(py::init<std::shared_ptr<Radio>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<SlotSynthesizer>,
                      double,
                      double,
                      double,
                      double>(),
            py::arg("radio"),
            py::arg("controller"),
            py::arg("snapshot_collector"),
            py::arg("channelizer"),
            py::arg("synthesizer"),
            py::arg("slot_size"),
            py::arg("guard_size"),
            py::arg("slot_send_lead_time"),
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
