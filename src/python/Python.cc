#include <pybind11/embed.h>

namespace py = pybind11;

#include "phy/Channels.hh"
#include "net/Net.hh"
#include "python/PyModules.hh"

// UGH. See:
//   https://stackoverflow.com/questions/240353/convert-a-preprocessor-token-to-a-string

#define TOSTRING2(s) #s
#define TOSTRING(s) TOSTRING2(s)

PYBIND11_EMBEDDED_MODULE(dragonradio, m) {
    // Export DragonRadio version
    m.attr("version") = TOSTRING(VERSION);

    // Export class Channels to Python
    py::bind_vector<std::vector<double>, std::shared_ptr<std::vector<double>>>(m, "Channels");

    exportLiquidEnums(m);
    exportLogger(m);
    exportRadioConfig(m);
    exportWorkQueue(m);
    exportUSRP(m);
    exportMCS(m);
    exportEstimators(m);
    exportNet(m);
    exportRadioNet(m);
    exportPHYs(m);
    exportPacketModulators(m);
    exportControllers(m);
    exportMACs(m);
    exportResamplers(m);
}
