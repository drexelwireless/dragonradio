#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

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
        .def("reconfigure",
            &MAC::reconfigure,
            "Force the MAC to reconfigure after PHY parameters, e.g., TX rate, change.")
        ;

    // Export class SlottedMAC to Python
    py::class_<SlottedMAC, MAC, std::shared_ptr<SlottedMAC>>(m, "SlottedMAC")
        .def_property("slot_size",
            &SlottedMAC::getSlotSize,
            &SlottedMAC::setSlotSize,
            "Slot size (sec)")
        .def_property("guard_size",
            &SlottedMAC::getGuardSize,
            &SlottedMAC::setGuardSize,
            "Guard size (sec)")
        .def_property("slot_modulate_lead_time",
            &SlottedMAC::getSlotModulateLeadTime,
            &SlottedMAC::setSlotModulateLeadTime,
            "Slot modulation lead time (sec)")
        .def_property("slot_send_lead_time",
            &SlottedMAC::getSlotSendLeadTime,
            &SlottedMAC::setSlotSendLeadTime,
            "Slot send lead time (sec)")
        ;

    // Export class TDMA to Python
    py::class_<TDMA, SlottedMAC, std::shared_ptr<TDMA>>(m, "TDMA")
        .def(py::init<std::shared_ptr<USRP>,
                      std::shared_ptr<PHY>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<Synthesizer>,
                      double,
                      double,
                      double,
                      double,
                      size_t>())
        .def_property("schedule",
            &TDMA::getSchedule,
            py::overload_cast<const Schedule::sched_type &>(&TDMA::setSchedule),
            "MAC schedule specifying on which channels this node may transmit in each schedule slot.")
        .def_property_readonly("nslots",
            &TDMA::getNSlots,
            "Number of TDMA slots.")
        .def_property("superslots",
            &TDMA::getSuperslots,
            &TDMA::setSuperslots,
            "Flag indicating whether or not to use superslots.");
        ;

    // Export class SlottedALOHA to Python
    py::class_<SlottedALOHA, SlottedMAC, std::shared_ptr<SlottedALOHA>>(m, "SlottedALOHA")
        .def(py::init<std::shared_ptr<USRP>,
                      std::shared_ptr<PHY>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<Synthesizer>,
                      double,
                      double,
                      double,
                      double,
                      double>())
        .def_property("p", &SlottedALOHA::getTXProb, &SlottedALOHA::setTXProb)
        ;
}
