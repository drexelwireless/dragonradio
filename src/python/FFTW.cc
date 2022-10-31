// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "dsp/FFTW.hh"
#include "python/PyModules.hh"

void exportFFTW(py::module &m)
{
    m.def("planFFTs",
          &fftw::planFFTs,
          py::call_guard<py::gil_scoped_release>(),
          "plan FFTs");

    m.def("exportWisdom",
          &fftw::exportWisdom,
          py::call_guard<py::gil_scoped_release>(),
          "export FFTW wisdom");
    m.def("importWisdom",
          &fftw::importWisdom,
          py::call_guard<py::gil_scoped_release>(),
          "import FFTW wisdom");
}
