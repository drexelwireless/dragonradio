// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "dsp/NCO.hh"
#include "dsp/TableNCO.hh"
#include "liquid/NCO.hh"
#include "python/PyModules.hh"

void exportNCOs(py::module &m)
{
    // Export class NCO to Python
    py::class_<NCO, std::shared_ptr<NCO>>(m, "NCO")
        .def_property("frequency",
            &NCO::getFrequency,
            &NCO::setFrequency,
            "Frequency in radians per sample")
        .def_property("phase",
            &NCO::getPhase,
            &NCO::setPhase,
            "Phase in radians")
        .def("reset",
            &NCO::reset,
            "Reset NCO state given frequency in radians per sample")
        .def("mix_up", [](NCO &nco, py::array_t<std::complex<float>> in) -> py::array_t<std::complex<float>> {
            auto inbuf = in.request();

            py::array_t<std::complex<float>> outarr(inbuf.size);
            auto                             outbuf = outarr.request();

            nco.mix_up(reinterpret_cast<std::complex<float>*>(inbuf.ptr),
                       reinterpret_cast<std::complex<float>*>(outbuf.ptr),
                       inbuf.size);

            return outarr;
        },
        "Mix signal up")
        .def("mix_down", [](NCO &nco, py::array_t<std::complex<float>> in) -> py::array_t<std::complex<float>> {
            auto inbuf = in.request();

            py::array_t<std::complex<float>> outarr(inbuf.size);
            auto                             outbuf = outarr.request();

            nco.mix_down(reinterpret_cast<std::complex<float>*>(inbuf.ptr),
                         reinterpret_cast<std::complex<float>*>(outbuf.ptr),
                         inbuf.size);

            return outarr;
        },
        "Mix signal down")
        ;

    // Export class LiquidNCOBase to Python
    py::class_<Liquid::BaseNCO, NCO, std::shared_ptr<Liquid::BaseNCO>>(m, "LiquidBaseNCO")
        ;

    // Export class LiquidNCO to Python
    py::class_<Liquid::NCO, Liquid::BaseNCO, std::shared_ptr<Liquid::NCO>>(m, "LiquidNCO")
        .def(py::init<double>())
        ;

    // Export class LiquidVCO to Python
    py::class_<Liquid::VCO, Liquid::BaseNCO, std::shared_ptr<Liquid::VCO>>(m, "LiquidVCO")
        .def(py::init<double>())
        ;

    // Export class TableNCO to Python
    py::class_<TableNCO, NCO, std::shared_ptr<TableNCO>>(m, "TableNCO")
        .def(py::init<double>())
        ;
}
