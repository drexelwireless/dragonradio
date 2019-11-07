#include <time.h>

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
        .def_property("schedule",
            &SlottedMAC::getSchedule,
            py::overload_cast<const Schedule::sched_type &>(&SlottedMAC::setSchedule),
            "MAC schedule specifying on which channels this node may transmit in each schedule slot.")
        .def_property("min_channel_bandwidth",
            nullptr,
            &MAC::setMinChannelBandwidth,
            "Minimum channel bandwidth")
        .def("getLoad",
            &SlottedMAC::getLoad,
            "Get current load")
        .def("popLoad",
            &SlottedMAC::popLoad,
            "Get current load and reset load counters")
        ;

    // Export class MAC::Load to Python
    using Load = MAC::Load;

    py::class_<Load, std::shared_ptr<Load>>(m, "Load")
        .def_property_readonly("start",
            [](Load &self) -> double
            {
                return self.start.get_real_secs();
            },
            "Start of load measurement period (sec)")
        .def_property_readonly("end",
            [](Load &self) -> double
            {
                return self.end.get_real_secs();
            },
            "End of load measurement period (sec)")
        .def_property_readonly("period",
            [](Load &self) -> double
            {
                return (self.end - self.start).get_real_secs();
            },
            "Measurement period (sec)")
        .def_readwrite("nsamples",
            &SlottedMAC::Load::nsamples,
            "Load per channel measured in number of samples")
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
                      std::shared_ptr<SlotSynthesizer>,
                      double,
                      double,
                      double,
                      size_t>())
        .def_property_readonly("nslots",
            &TDMA::getNSlots,
            "The number of TDMA slots.")
        ;

    // Export class SlottedALOHA to Python
    py::class_<SlottedALOHA, SlottedMAC, std::shared_ptr<SlottedALOHA>>(m, "SlottedALOHA")
        .def(py::init<std::shared_ptr<USRP>,
                      std::shared_ptr<PHY>,
                      std::shared_ptr<Controller>,
                      std::shared_ptr<SnapshotCollector>,
                      std::shared_ptr<Channelizer>,
                      std::shared_ptr<SlotSynthesizer>,
                      double,
                      double,
                      double,
                      double>())
        .def_property("slotidx",
            &SlottedALOHA::getSlotIndex,
            &SlottedALOHA::setSlotIndex,
            "Slot index to transmit in")
        .def_property("p",
            &SlottedALOHA::getTXProb,
            &SlottedALOHA::setTXProb,
            "Probability of transmission in a given slot")
        ;
}
