#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "dsp/Filter.hh"
#include "liquid/Filter.hh"
#include "python/PyModules.hh"

void exportFilters(py::module &m)
{
    // Export filter classes to Python
    py::class_<Filter, std::unique_ptr<Filter>>(m, "Filter")
        .def("groupDelay", &Filter::getGroupDelay)
        .def("reset", &Filter::reset, "Reset the filter's state")
        .def("execute", [](Filter &filt, py::array_t<std::complex<float>> in) -> py::array_t<std::complex<float>> {
            auto inbuf = in.request();

            py::array_t<std::complex<float>> outarr(inbuf.size);
            auto                             outbuf = outarr.request();

            filt.execute(reinterpret_cast<std::complex<float>*>(inbuf.ptr),
                         reinterpret_cast<std::complex<float>*>(outbuf.ptr),
                         inbuf.size);

            return outarr;
        },
        "Execute the filter")
        ;

    py::class_<FIRFilter, Filter, std::unique_ptr<FIRFilter>>(m, "FIRFilter")
        .def_property_readonly("delay", &FIRFilter::getDelay)
        ;

    py::class_<IIRFilter, Filter, std::unique_ptr<IIRFilter>>(m, "IIRFilter")
        ;

    py::class_<Liquid::FIRFilter, FIRFilter, std::unique_ptr<Liquid::FIRFilter>>(m, "LiquidFIRFilter")
        .def(py::init<const std::vector<std::complex<float>>&>())
        ;

    py::class_<Liquid::IIRFilter, IIRFilter, std::unique_ptr<Liquid::IIRFilter>>(m, "LiquidIIRFilter")
        .def(py::init([](py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> b,
                         py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> a) {
            py::buffer_info b_buf = b.request();
            py::buffer_info a_buf = b.request();

            if (b_buf.ndim != 1 || a_buf.ndim != 1)
                throw std::runtime_error("Number of dimensions must be one");

            if (a_buf.size != b_buf.size)
                throw std::runtime_error("Input shapes must match");

            return Liquid::IIRFilter(static_cast<std::complex<float>*>(b_buf.ptr), b_buf.size,
                                     static_cast<std::complex<float>*>(a_buf.ptr), a_buf.size);
        }))
        .def(py::init([](py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> sos) {
            py::buffer_info sos_buf = sos.request();

            if (sos_buf.ndim != 2 || sos_buf.shape[1] != 6)
                throw std::runtime_error("SOS array must have shape Nx6");

            return Liquid::IIRFilter(static_cast<std::complex<float>*>(sos_buf.ptr), sos_buf.size/6);
        }))
        ;

    m.def("parks_mcclellan", &Liquid::parks_mcclellan);

    m.def("kaiser", &Liquid::kaiser);

    m.def("butter_lowpass", &Liquid::butter_lowpass);
}
