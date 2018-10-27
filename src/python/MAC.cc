#include "mac/SlottedALOHA.hh"
#include "mac/SlottedMAC.hh"
#include "mac/TDMA.hh"
#include "python/PyModules.hh"

void exportMACs(py::module &m)
{
    // Export class MAC to Python
    py::class_<MAC, std::shared_ptr<MAC>>(m, "MAC")
        .def_property("rx_channels", &MAC::getRXChannels, &MAC::setRXChannels)
        .def_property("tx_channels", &MAC::getTXChannels, &MAC::setTXChannels)
        .def_property("tx_channel", &MAC::getTXChannel, &MAC::setTXChannel)
        ;

    // Export class SlottedMAC to Python
    py::class_<SlottedMAC, MAC, std::shared_ptr<SlottedMAC>>(m, "SlottedMAC")
        .def_property("slot_size", &SlottedMAC::getSlotSize, &SlottedMAC::setSlotSize)
        .def_property("guard_size", &SlottedMAC::getGuardSize, &SlottedMAC::setGuardSize)
        .def_property("demod_overlap_size", &SlottedMAC::getDemodOverlapSize, &SlottedMAC::setDemodOverlapSize)
        .def_property("premod_slots", &SlottedMAC::getPreModulateSlots, &SlottedMAC::setPreModulateSlots)
        ;

    // Export class TDMA::Slots to Python
    py::class_<TDMA::Slots, std::shared_ptr<TDMA::Slots>>(m, "Slots")
        .def("__getitem__", [](TDMA::Slots &slots, TDMA::Slots::slots_type::size_type i) {
            try {
                return slots[i];
            } catch (const std::out_of_range&) {
                throw py::index_error();
            }
        })
        .def("__setitem__", [](TDMA::Slots &slots, TDMA::Slots::slots_type::size_type i, bool v) {
            try {
                slots[i] = v;
            } catch (const std::out_of_range&) {
              throw py::index_error();
            }
        })
        .def("__len__", &TDMA::Slots::size)
        .def("__iter__", [](TDMA::Slots &slots) {
            return py::make_iterator(slots.begin(), slots.end());
         }, py::keep_alive<0, 1>())
        .def("resize", &TDMA::Slots::resize)
        ;

    // Export class TDMA to Python
    py::class_<TDMA, SlottedMAC, std::shared_ptr<TDMA>>(m, "TDMA")
        .def(py::init<std::shared_ptr<USRP>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      const Channels&,
                      std::shared_ptr<PacketModulator>,
                      std::shared_ptr<PacketDemodulator>,
                      double,
                      double,
                      double,
                      size_t>())
        .def_property_readonly("slots", &TDMA::getSlots,
            py::return_value_policy::reference_internal);
        ;

    // Export class SlottedALOHA to Python
    py::class_<SlottedALOHA, SlottedMAC, std::shared_ptr<SlottedALOHA>>(m, "SlottedALOHA")
        .def(py::init<std::shared_ptr<USRP>,
                      std::shared_ptr<PHY>,
                      const Channels&,
                      const Channels&,
                      std::shared_ptr<PacketModulator>,
                      std::shared_ptr<PacketDemodulator>,
                      double,
                      double,
                      double,
                      double>())
        .def_property("p", &SlottedALOHA::getTXProb, &SlottedALOHA::setTXProb)
        ;
}
