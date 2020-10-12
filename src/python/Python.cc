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
    exportRadioConfig(mradio);
    exportWorkQueue(mradio);
    exportUSRP(mradio);
    exportEstimators(mradio);
    exportNet(mradio);
    exportCIL(mradio);
    exportFlow(mradio);
    exportRadioNet(mradio);
    exportHeader(mradio);
    exportPacket(mradio);
    exportModem(mradio);
    exportPHYs(mradio);
    exportLiquid(mliquid);
    exportLiquidPHYs(mliquid);
    exportChannels(mradio);
    exportChannelizers(mradio);
    exportSynthesizers(mradio);
    exportControllers(mradio);
    exportMACs(mradio);
    exportResamplers(mradio);
    exportNCOs(mradio);
    exportFilters(mradio);
    exportIQBuffer(mradio);
    exportIQCompression(mradio);
    exportSnapshot(mradio);
#endif /* !defined(PYMODULE) */
}
