// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <firpm/pm.h>

#include "dsp/FIR.hh"
#include "dsp/FIRDesign.hh"
#include "dsp/Filter.hh"
#include "dsp/Window.hh"
#include "liquid/Filter.hh"
#include "python/PyModules.hh"

template <class I, class O>
void exportFilter(py::module &m, const char *name)
{
    using pyarray_I = py::array_t<I, py::array::c_style | py::array::forcecast>;
    using pyarray_O = py::array_t<O>;

    py::class_<Filter<I,O>, std::unique_ptr<Filter<I,O>>>(m, name)
        .def("groupDelay",
            &Filter<I,O>::getGroupDelay,
            "Return filter group delay of given frequency")
        .def("reset",
            &Filter<I,O>::reset,
            "Reset the filter's state")
        .def("execute",
            [](Filter<I,O> &filt, pyarray_I in) -> pyarray_O
            {
                auto                   inbuf = in.request();
                pyarray_O              outarr(inbuf.size);
                auto                   outbuf = outarr.request();
                py::gil_scoped_release release;

                filt.execute(reinterpret_cast<I*>(inbuf.ptr),
                             reinterpret_cast<O*>(outbuf.ptr),
                             inbuf.size);

                return outarr;
            },
            "Execute the filter")
        ;
}

template <class I, class O, class C>
void exportLiquidFIR(py::module &m, const char *name)
{
    py::class_<liquid::FIR<I,O,C>, Filter<I,O>, std::unique_ptr<liquid::FIR<I,O,C>>>(m, name)
        .def(py::init<const std::vector<C>&>())
        .def_property_readonly("delay",
            &FIR<I,O,C>::getDelay,
            "Return filter delay")
        .def_property("taps",
            &FIR<I,O,C>::getTaps,
            &FIR<I,O,C>::setTaps,
            "Filter taps")
        ;
}

template <class I, class O, class C>
void exportLiquidIIR(py::module &m, const char *name)
{
    using pyarray_C = py::array_t<C, py::array::c_style | py::array::forcecast>;

    py::class_<liquid::IIR<I,O,C>, Filter<I,O>, std::unique_ptr<liquid::IIR<I,O,C>>>(m, name)
        .def(py::init([](pyarray_C b, pyarray_C a) {
            py::buffer_info        b_buf = b.request();
            py::buffer_info        a_buf = b.request();
            py::gil_scoped_release release;

            if (b_buf.ndim != 1 || a_buf.ndim != 1)
                throw std::runtime_error("Number of dimensions must be one");

            if (a_buf.size != b_buf.size)
                throw std::runtime_error("Input shapes must match");

            return liquid::IIR<I,O,C>(static_cast<C*>(b_buf.ptr), b_buf.size,
                                      static_cast<C*>(a_buf.ptr), a_buf.size);
        }))
        .def(py::init([](pyarray_C sos) {
            py::buffer_info        sos_buf = sos.request();
            py::gil_scoped_release release;

            if (sos_buf.ndim != 2 || sos_buf.shape[1] != 6)
                throw std::runtime_error("SOS array must have shape Nx6");

            return liquid::IIR<I,O,C>(static_cast<C*>(sos_buf.ptr), sos_buf.size/6);
        }))
        ;
}

template <class T, class C>
void exportDragonFIR(py::module &m, const char *name)
{
    py::class_<dragonradio::signal::FIR<T,C>, Filter<T,T>, std::unique_ptr<dragonradio::signal::FIR<T,C>>>(m, name)
        .def(py::init<const std::vector<C>&>())
        .def_property_readonly("delay",
            &dragonradio::signal::FIR<T,C>::getDelay,
            "Return filter delay")
        .def_property("taps",
            &dragonradio::signal::FIR<T,C>::getTaps,
            &dragonradio::signal::FIR<T,C>::setTaps,
            "Filter taps")
        ;
}

template <class T>
void exportWindow(py::module &m, const char *name)
{
    py::class_<Window<T>, std::unique_ptr<Window<T>>>(m, name)
        .def(py::init<typename Window<T>::size_type>())
        .def_property_readonly("size",
            &Window<T>::size,
            "Window size")
        .def("resize",
            &Window<T>::resize,
            "Resize window")
        .def("reset",
            &Window<T>::reset,
            "Reset window")
        .def("add",
            &Window<T>::add,
            "Add element to window")
        .def_property_readonly("window",
            &Window<T>::get,
            "Get values in the window")
        ;
}

void exportFirpm(py::module &m)
{
    py::enum_<pm::init_t>(m, "Strategy")
        .value("UNIFORM", pm::init_t::UNIFORM)
        .value("SCALING", pm::init_t::SCALING)
        .value("AFP", pm::init_t::AFP)
        .export_values();

    py::enum_<pm::status_t>(m, "Status")
        .value("SUCCESS", pm::status_t::STATUS_SUCCESS)
        .value("FREQUENCY_INVALID_INTERVAL", pm::status_t::STATUS_FREQUENCY_INVALID_INTERVAL)
        .value("AMPLITUDE_VECTOR_MISMATCH", pm::status_t::STATUS_AMPLITUDE_VECTOR_MISMATCH)
        .value("AMPLITUDE_DISCONTINUITY", pm::status_t::STATUS_AMPLITUDE_DISCONTINUITY)
        .value("WEIGHT_NEGATIVE", pm::status_t::STATUS_WEIGHT_NEGATIVE)
        .value("WEIGHT_VECTOR_MISMATCH", pm::status_t::STATUS_WEIGHT_VECTOR_MISMATCH)
        .value("WEIGHT_DISCONTINUITY", pm::status_t::STATUS_WEIGHT_DISCONTINUITY)
        .value("SCALING_INVALID", pm::status_t::STATUS_SCALING_INVALID)
        .value("AFP_INVALID", pm::status_t::STATUS_AFP_INVALID)
        .value("COEFFICIENT_SET_INVALID", pm::status_t::STATUS_COEFFICIENT_SET_INVALID)
        .value("EXCHANGE_FAILURE", pm::status_t::STATUS_EXCHANGE_FAILURE)
        .value("CONVERGENCE_WARNING", pm::status_t::STATUS_CONVERGENCE_WARNING)
        .value("UNKNOWN_FAILURE", pm::status_t::STATUS_UNKNOWN_FAILURE)
        .export_values();

    py::class_<pm::pmoutput_t<double>>(m, "PMOutput")
        .def_readonly("h",
            &pm::pmoutput_t<double>::h,
            "Final filter coefficients")
        .def_readonly("x",
            &pm::pmoutput_t<double>::x,
            "Reference set used to generate the final filter")
        .def_readonly("iter",
            &pm::pmoutput_t<double>::iter,
            "Number of iterations that were necessary to achieve convergence")
        .def_readonly("delta",
            &pm::pmoutput_t<double>::delta,
            "The final reference error")
        .def_readonly("q",
            &pm::pmoutput_t<double>::q,
            "convergence parameter value")
        .def_readonly("q",
            &pm::pmoutput_t<double>::status,
            "status code for the output object")
        .def("__repr__", [](const pm::pmoutput_t<double>& self) {
            return py::str("PMOutput(h={}, x={}, iter={}, delta={}, q={}, status={})").
                format(self.h, self.x, self.iter, self.delta, self.q, self.status);
         })
        ;

    m.def("firpm",
        &dragonradio::signal::firpm,
        "Use the Remez exchange algorithm to design an equiripple filter",
        py::arg("n"),
        py::arg("f"),
        py::arg("a"),
        py::arg("w"),
        py::arg("fs") = 2.0,
        py::arg("eps") = 0.01,
        py::arg("nmax") = 4,
        py::arg("strategy") = pm::init_t::UNIFORM,
        py::arg("depth") = 0u,
        py::arg("rstrategy") = pm::init_t::UNIFORM,
        py::arg("prec") = 165ul,
        py::call_guard<py::gil_scoped_release>());

    m.def("firpm1f",
        &dragonradio::signal::firpm1f,
        "Use the Remez exchange algorithm to design a filter with 1/f rolloff",
        py::arg("n"),
        py::arg("f"),
        py::arg("a"),
        py::arg("w"),
        py::arg("fs") = 2.0,
        py::arg("eps") = 0.01,
        py::arg("nmax") = 4,
        py::arg("strategy") = pm::init_t::UNIFORM,
        py::arg("depth") = 0u,
        py::arg("rstrategy") = pm::init_t::UNIFORM,
        py::arg("prec") = 165ul,
        py::call_guard<py::gil_scoped_release>());

    m.def("firpm1f2",
        &dragonradio::signal::firpm1f2,
        "Use the Remez exchange algorithm to design a filter with 1/f^2 rolloff",
        py::arg("n"),
        py::arg("f"),
        py::arg("a"),
        py::arg("w"),
        py::arg("fs") = 2.0,
        py::arg("eps") = 0.01,
        py::arg("nmax") = 4,
        py::arg("strategy") = pm::init_t::UNIFORM,
        py::arg("depth") = 0u,
        py::arg("rstrategy") = pm::init_t::UNIFORM,
        py::arg("prec") = 165ul,
        py::call_guard<py::gil_scoped_release>());
}

void exportFilters(py::module &m)
{
    using C = std::complex<float>;
    using F = float;

    exportFilter<C,C>(m, "FilterCC");
    exportDragonFIR<C,C>(m, "FIRCCC");
    exportDragonFIR<C,F>(m, "FIRCCF");
    exportLiquidFIR<C,C,C>(m, "LiquidFIRCCC");
    exportLiquidIIR<C,C,C>(m, "LiquidIIRCCC");

    m.def("parks_mcclellan", &liquid::parks_mcclellan);

    m.def("kaiser", &liquid::kaiser);

    m.def("butter_lowpass", &liquid::butter_lowpass);

    exportWindow<C>(m, "WindowC");

    auto mfirpm = m.def_submodule("firpm");

    exportFirpm(mfirpm);
}
