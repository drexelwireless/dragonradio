#include <liquid/liquid.h>

#include "RadioConfig.hh"
#include "python/PyModules.hh"

void exportRadioConfig(py::module &m)
{
    // Export class RadioConfig to Python
    py::class_<RadioConfig, std::shared_ptr<RadioConfig>>(m, "RadioConfig")
        .def(py::init())
        .def_readwrite("verbose",
            &RadioConfig::verbose,
            "Output verbose messages to the console")
        .def_readwrite("debug",
            &RadioConfig::debug,
            "Output debug messages to the console")
        .def_readwrite("log_invalid_headers",
            &RadioConfig::log_invalid_headers,
            "Log invalid headers?")
        .def_readwrite("mtu",
            &RadioConfig::mtu,
            "Maximum Transmission Unit (bytes)")
        .def_readwrite("verbose_packet_trace",
            &RadioConfig::verbose_packet_trace,
            "Display packets written to tun/tap device?")
        ;

    // Export our global RadioConfig
    m.attr("rc") = py::cast(rc, py::return_value_policy::reference);
}
