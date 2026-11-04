// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/stl.h>

#include "WorkQueue.hh"
#include "util/WorkMeter.hh"
#include "python/PyModules.hh"

void exportWorkQueue(py::module &m)
{
    // Export class WorkQueue to Python
    py::class_<WorkQueue, std::shared_ptr<WorkQueue>>(m, "WorkQueue")
        .def("addThreads", &WorkQueue::addThreads,
            "Add workers")
        ;

    // Export our global WorkQueue
    m.attr("work_queue") = py::cast(work_queue, py::return_value_policy::reference);

    // Work-time meters of the channelizer, synthesizer and demodulator
    // workers (seconds of processing, excluding spin-waits), by thread name.
    m.def("work_meters", &WorkMeter::snapshot,
        "Seconds each named worker has spent processing (util/WorkMeter.hh)");
}
