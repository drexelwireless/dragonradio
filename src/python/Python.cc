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
    // Create submodule for liquid
    auto mliquid = m.def_submodule("liquid");

#if defined(PYMODULE)
    exportResamplers(m);
    exportNCOs(m);
    exportFilters(m);
    exportIQCompression(m);
    exportChannels(m);
    exportHeader(m);
    exportModem(m);
    exportLiquid(mliquid);
#else /* !defined(PYMODULE) */
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
#endif /* !defined(PYMODULE) */
}
