// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "mac/Snapshot.hh"
#include "python/PyModules.hh"

#if !defined(DOXYGEN)
PYBIND11_MAKE_OPAQUE(std::vector<SelfTX>)
#endif /* !defined(DOXYGEN) */

void exportSnapshot(py::module &m)
{
    // Export class Snapshot to Python
    py::class_<Snapshot, std::shared_ptr<Snapshot>>(m, "Snapshot")
        .def_readonly("timestamp",
            &Snapshot::timestamp,
            "datetime.timedelta: Snapshot timestamp")
        .def_readonly("slots",
            &Snapshot::slots,
            "Sequence[IQBuf]: Slots in snapshot (IQ data)")
        .def_readonly("selftx",
            &Snapshot::selftx,
            "Sequence[SelfTX]: Self-transmission events")
        .def_property_readonly("combined_slots",
            &Snapshot::getCombinedSlots,
            "Optional[IQBuf]: Combined IQ data for all slots in snapshot")
        .def("__repr__", [](const Snapshot& self) {
            return py::str("Snapshot(timestamp={})").format(self.timestamp);
         })
        ;

    // Export class Snapshot::SelfTX to Python
    py::class_<SelfTX, std::shared_ptr<SelfTX>>(m, "SelfTX")
        .def_readwrite("start",
            &SelfTX::start,
            "int: Snapshot sample offset of start of packet")
        .def_readwrite("end",
            &SelfTX::end,
            "int: Snapshot sample offset of end of packet")
        .def_readwrite("fc",
            &SelfTX::fc,
            "float: Center frequency of packet")
        .def_readwrite("fs",
            &SelfTX::fs,
            "float: Sample frequency of packet")
        .def("__repr__", [](const SelfTX& self) {
            return py::str("SelfTX(start={}, end={}, fc={:g}, fs={:g})").format(self.start, self.end, self.fc, self.fs);
         })
        ;

    // Export class SnapshotCollector to Python
    py::class_<SnapshotCollector, std::shared_ptr<SnapshotCollector>>(m, "SnapshotCollector")
        .def(py::init())
        .def("start",
            &SnapshotCollector::start,
            "Start snapshot collection")
        .def("next",
            &SnapshotCollector::next,
            "Get current snapshot and start a new snapshot immediately")
        .def("stop",
            &SnapshotCollector::stop,
            "Stop snapshot collection")
        .def("finalize",
            &SnapshotCollector::finalize,
            "Finalize snapshot collection, returning the collected snapshot")
        .def_property_readonly("active",
            &SnapshotCollector::active,
            "bool: Is snapshot collection active?")
        ;
}
