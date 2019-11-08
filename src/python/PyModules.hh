#ifndef PYMODULES_H_
#define PYMODULES_H_

#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>

#include "net/Element.hh"
#include "phy/Channel.hh"
#include "python/capsule.hh"

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(Channels)

template <class D, class P, class T>
struct PortWrapper
{
    std::shared_ptr<Element> element;
    Port<D, P, T> *port;

    template <class U>
    PortWrapper(std::shared_ptr<U> e, Port<D, P, T> *p) :
        element(std::static_pointer_cast<Element>(e)), port(p) {}
    ~PortWrapper() = default;

    PortWrapper() = delete;
};

template <class U, class D, class P, class T>
std::unique_ptr<PortWrapper<D,P,T>> exposePort(std::shared_ptr<U> e, Port<D, P, T> *p)
{
    return std::make_unique<PortWrapper<D,P,T>>(std::static_pointer_cast<Element>(e), p);
}

template <typename D>
using NetInWrapper = PortWrapper<In,D,std::shared_ptr<NetPacket>>;

template <typename D>
using NetOutWrapper = PortWrapper<Out,D,std::shared_ptr<NetPacket>>;

using NetInPush = NetInWrapper<Push>;
using NetInPull = NetInWrapper<Pull>;
using NetOutPush = NetOutWrapper<Push>;
using NetOutPull = NetOutWrapper<Pull>;

template <typename D>
using RadioInWrapper = PortWrapper<In,D,std::shared_ptr<RadioPacket>>;

template <typename D>
using RadioOutWrapper = PortWrapper<Out,D,std::shared_ptr<RadioPacket>>;

using RadioInPush = RadioInWrapper<Push>;
using RadioInPull = RadioInWrapper<Pull>;
using RadioOutPush = RadioOutWrapper<Push>;
using RadioOutPull = RadioOutWrapper<Pull>;

void exportClock(py::module &m);
void exportLogger(py::module &m);
void exportRadioConfig(py::module &m);
void exportWorkQueue(py::module &m);
void exportUSRP(py::module &m);
void exportEstimators(py::module &m);
void exportNet(py::module &m);
void exportCIL(py::module &m);
void exportFlow(py::module &m);
void exportRadioNet(py::module &m);
void exportHeader(py::module &m);
void exportPacket(py::module &m);
void exportModem(py::module &m);
void exportLiquid(py::module &m);
void exportPHYs(py::module &m);
void exportLiquidPHYs(py::module &m);
void exportChannels(py::module &m);
void exportChannelizers(py::module &m);
void exportSynthesizers(py::module &m);
void exportControllers(py::module &m);
void exportMACs(py::module &m);
void exportResamplers(py::module &m);
void exportNCOs(py::module &m);
void exportFilters(py::module &m);
void exportIQBuffer(py::module &m);
void exportIQCompression(py::module &m);
void exportSnapshot(py::module &m);

#endif /* PYMODULES_H_ */
