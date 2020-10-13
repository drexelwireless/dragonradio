// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <liquid/liquid.h>

#include "RadioConfig.hh"
#include "mac/Snapshot.hh"
#include "python/PyModules.hh"

void exportRadioConfig(py::module &m)
{
    // Export class RadioConfig to Python
    py::class_<RadioConfig, std::shared_ptr<RadioConfig>>(m, "RadioConfig")
        .def(py::init())
        .def_readwrite("node_id",
            &RadioConfig::node_id,
            "Current node's ID")
        .def_readwrite("snapshot_collector",
            &RadioConfig::snapshot_collector,
            "Snapshot collector")
        ;

    // Export our global RadioConfig
    m.attr("rc") = py::cast(rc, py::return_value_policy::reference);
}
