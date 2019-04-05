#include <pybind11/embed.h>

namespace py = pybind11;

#include "phy/Channel.hh"
#include "net/Net.hh"
#include "python/PyModules.hh"

// UGH. See:
//   https://stackoverflow.com/questions/240353/convert-a-preprocessor-token-to-a-string

#define TOSTRING2(s) #s
#define TOSTRING(s) TOSTRING2(s)

PYBIND11_EMBEDDED_MODULE(dragonradio, m) {
    // Export DragonRadio version
    m.attr("version") = TOSTRING(VERSION);

    exportLiquidEnums(m);
    exportClock(m);
    exportLogger(m);
    exportRadioConfig(m);
    exportWorkQueue(m);
    exportUSRP(m);
    exportMCS(m);
    exportEstimators(m);
    exportNet(m);
    exportFlow(m);
    exportRadioNet(m);
    exportPHYs(m);
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
