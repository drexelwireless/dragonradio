// Copyright 2018-2023 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/embed.h>

namespace py = pybind11;

#include "python/PyModules.hh"

#if defined(DRAGONRADIO_NOTEBOOK)
void exportNotebook(py::module &m);
#endif /* defined(DRAGONRADIO_NOTEBOOK) */

#if defined(PYMODULE)
PYBIND11_MODULE(dragonradiocpp, m) {
#else /* !defined(PYMODULE) */
PYBIND11_EMBEDDED_MODULE(dragonradiocpp, m) {
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

    exportChannels(mradio);

    exportHeader(mpacket);

    exportModem(mradio);

    exportLiquid(mliquid);
#else /* !defined(PYMODULE) */
    exportFFTW(msignal);
    exportResamplers(msignal);
    exportNCOs(msignal);
    exportFilters(msignal);
    exportIQCompression(msignal);

    exportChannels(mradio);

    exportHeader(mpacket);
    exportPacket(mpacket);

    exportSnapshot(mradio);

    exportLogger(mlogging);

    exportClock(mradio);
    exportWorkQueue(mradio);
    exportRadio(mradio);
    exportUSRP(mradio);
    exportEstimators(mradio);
    exportElement(mradio);
    exportPHYs(mradio);
    exportControllers(mradio);
    exportCIL(mradio);
    exportNet(mradio);
    exportFlow(mradio);
    exportNode(mradio);
    exportNeighborhood(mradio);
    exportModem(mradio);
    exportChannelizers(mradio);
    exportSynthesizers(mradio);
    exportMACs(mradio);
    exportIQBuffer(mradio);

    exportLiquid(mliquid);
    exportLiquidPHYs(mliquid);

    exportNativePHYs(mradio);

    exportSim(mradio);

    exportNetUtil(mnet);

#if defined(DRAGONRADIO_NOTEBOOK)
    auto mnotebook = m.def_submodule("notebook");

    exportNotebook(mnotebook);
#endif /* defined(DRAGONRADIO_NOTEBOOK) */
#endif /* !defined(PYMODULE) */
}
