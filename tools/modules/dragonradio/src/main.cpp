#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <liquid/liquid.h>

#include "python/PyModules.hh"

namespace py = pybind11;

PYBIND11_MODULE(dragonradio, m) {
    // Create submodule for liquid
    auto mliquid = m.def_submodule("liquid");

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif

    exportResamplers(m);
    exportNCOs(m);
    exportFilters(m);
    exportIQCompression(m);
    exportModem(m);
    exportLiquid(mliquid);
}
