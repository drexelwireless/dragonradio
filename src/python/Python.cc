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
    auto mlogging = m.def_submodule("logging");
    auto mnet = m.def_submodule("net");
    auto mpacket = m.def_submodule("packet");
    auto mradio = m.def_submodule("radio");
    auto msignal = m.def_submodule("signal");

#if defined(PYMODULE)
    exportFFTW(msignal);
    exportResamplers(msignal);
    exportNCOs(msignal);
    exportFilters(msignal);
    exportIQCompression(msignal);

    exportHeader(mpacket);

    exportChannels(mradio);
    exportModem(mradio);

    exportLiquid(mliquid);
#else /* !defined(PYMODULE) */
    exportFFTW(msignal);
    exportResamplers(msignal);
    exportNCOs(msignal);
    exportFilters(msignal);
    exportIQCompression(msignal);

    exportHeader(mpacket);
    exportPacket(mpacket);

    exportLogger(mlogging);

    exportClock(mradio);
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
    exportModem(mradio);
    exportPHYs(mradio);
    exportChannels(mradio);
    exportChannelizers(mradio);
    exportSynthesizers(mradio);
    exportMACs(mradio);
    exportIQBuffer(mradio);
    exportSnapshot(mradio);

    exportLiquid(mliquid);
    exportLiquidPHYs(mliquid);

    exportNetUtil(mnet);
#endif /* !defined(PYMODULE) */
}
