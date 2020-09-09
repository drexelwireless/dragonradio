#include <pybind11/embed.h>

namespace py = pybind11;

#include "python/PyModules.hh"

// UGH. See:
//   https://stackoverflow.com/questions/240353/convert-a-preprocessor-token-to-a-string

#define TOSTRING2(s) #s
#define TOSTRING(s) TOSTRING2(s)

PYBIND11_EMBEDDED_MODULE(_dragonradio, m) {
    // Create submodule for liquid
    auto mliquid = m.def_submodule("liquid");

    // Export DragonRadio version
    m.attr("__version__") = TOSTRING(VERSION);

    exportClock(m);
    exportLogger(m);
    exportRadioConfig(m);
    exportWorkQueue(m);
    exportUSRP(m);
    exportEstimators(m);
    exportNet(m);
    exportCIL(m);
    exportFlow(m);
    exportRadioNet(m);
    exportHeader(m);
    exportPacket(m);
    exportModem(m);
    exportPHYs(m);
    exportLiquid(mliquid);
    exportLiquidPHYs(mliquid);
    exportChannels(m);
    exportChannelizers(m);
    exportSynthesizers(m);
    exportControllers(m);
    exportMACs(m);
    exportResamplers(m);
    exportNCOs(m);
    exportFilters(m);
    exportIQBuffer(m);
    exportIQCompression(m);
    exportSnapshot(m);
}
