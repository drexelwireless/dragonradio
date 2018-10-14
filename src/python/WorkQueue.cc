#include "WorkQueue.hh"
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
}
