#include "USRP.hh"
#include "python/PyModules.hh"

void exportUSRP(py::module &m)
{
    // Export class USRP to Python
    py::enum_<USRP::DeviceType>(m, "DeviceType")
        .value("N210", USRP::kUSRPN210)
        .value("X310", USRP::kUSRPX310)
        .value("Unknown", USRP::kUSRPUnknown)
        .export_values();

    py::implicitly_convertible<py::str, USRP::DeviceType>();

    py::class_<USRP, std::shared_ptr<USRP>>(m, "USRP")
        .def(py::init<const std::string&,
                      double,
                      const std::string&,
                      const std::string&,
                      float,
                      float>())
        .def_property_readonly("device_type",
            &USRP::getDeviceType)
        .def_property_readonly("clock_source",
            &USRP::getClockSource)
        .def_property_readonly("clock_rate",
            &USRP::getMasterClockRate)
        .def_property("tx_frequency",
            &USRP::getTXFrequency,
            &USRP::setTXFrequency)
        .def_property("rx_frequency",
            &USRP::getRXFrequency,
            &USRP::setRXFrequency)
        .def_property("tx_rate",
            &USRP::getTXRate,
            &USRP::setTXRate)
        .def_property("rx_rate",
            &USRP::getRXRate,
            &USRP::setRXRate)
        .def_property("tx_gain",
            &USRP::getTXGain,
            &USRP::setTXGain)
        .def_property("rx_gain",
            &USRP::getRXGain,
            &USRP::setRXGain)
        .def_property("tx_max_samps",
            &USRP::getMaxTXSamps,
            &USRP::setMaxTXSamps)
        .def_property("rx_max_samps",
            &USRP::getMaxRXSamps,
            &USRP::setMaxRXSamps)
        .def_property("auto_dc_offset",
            &USRP::getAutoDCOffset,
            &USRP::setAutoDCOffset)
        ;
}
