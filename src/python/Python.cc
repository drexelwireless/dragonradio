// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/embed.h>

namespace py = pybind11;

#include "python/PyModules.hh"

#if defined(PYMODULE)
PYBIND11_MODULE(_dragonradio, m) {
#else /* !defined(PYMODULE) */
PYBIND11_EMBEDDED_MODULE(_dragonradio, m) {
#endif /* !defined(PYMODULE) */
    auto mliquid = m.def_submodule("liquid");
    auto mradio = m.def_submodule("radio");
    auto mlogging = m.def_submodule("logging");
    auto mnet = m.def_submodule("net");

#if defined(PYMODULE)
    exportResamplers(mradio);
    exportNCOs(mradio);
    exportFilters(mradio);
    exportIQCompression(mradio);
    exportChannels(mradio);
    exportHeader(mradio);
    exportModem(mradio);
    exportLiquid(mliquid);
#else /* !defined(PYMODULE) */
    exportClock(mradio);
    exportLogger(mlogging);
    exportWorkQueue(mradio);
    exportRadio(mradio);
    exportUSRP(mradio);
    exportEstimators(mradio);
    exportControllers(mradio);
    exportNet(mradio);
    exportCIL(mradio);
    exportFlow(mradio);
    exportNode(mradio);
    exportNeighborhood(mradio);
    exportHeader(mradio);
    exportPacket(mradio);
    exportModem(mradio);
    exportPHYs(mradio);
    exportLiquid(mliquid);
    exportLiquidPHYs(mliquid);
    exportChannels(mradio);
    exportChannelizers(mradio);
    exportSynthesizers(mradio);
    exportMACs(mradio);
    exportResamplers(mradio);
    exportNCOs(mradio);
    exportFilters(mradio);
    exportIQBuffer(mradio);
    exportIQCompression(mradio);
    exportSnapshot(mradio);
    exportNetUtil(mnet);
#endif /* !defined(PYMODULE) */
}
