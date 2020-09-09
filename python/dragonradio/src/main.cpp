#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <liquid/liquid.h>

#include "python/PyModules.hh"

namespace py = pybind11;

PYBIND11_MODULE(_dragonradio, m) {
    // Create submodule for liquid
    auto mliquid = m.def_submodule("liquid");

    exportResamplers(m);
    exportNCOs(m);
    exportFilters(m);
    exportIQCompression(m);
    exportModem(m);
    exportLiquid(mliquid);
}
