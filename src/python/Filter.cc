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
                auto      inbuf = in.request();
                pyarray_O outarr(inbuf.size);
                auto      outbuf = outarr.request();

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
    py::class_<Liquid::FIR<I,O,C>, Filter<I,O>, std::unique_ptr<Liquid::FIR<I,O,C>>>(m, name)
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

    py::class_<Liquid::IIR<I,O,C>, Filter<I,O>, std::unique_ptr<Liquid::IIR<I,O,C>>>(m, name)
        .def(py::init([](pyarray_C b, pyarray_C a) {
            py::buffer_info b_buf = b.request();
            py::buffer_info a_buf = b.request();

            if (b_buf.ndim != 1 || a_buf.ndim != 1)
                throw std::runtime_error("Number of dimensions must be one");

            if (a_buf.size != b_buf.size)
                throw std::runtime_error("Input shapes must match");

            return Liquid::IIR<I,O,C>(static_cast<C*>(b_buf.ptr), b_buf.size,
                                      static_cast<C*>(a_buf.ptr), a_buf.size);
        }))
        .def(py::init([](pyarray_C sos) {
            py::buffer_info sos_buf = sos.request();

            if (sos_buf.ndim != 2 || sos_buf.shape[1] != 6)
                throw std::runtime_error("SOS array must have shape Nx6");

            return Liquid::IIR<I,O,C>(static_cast<C*>(sos_buf.ptr), sos_buf.size/6);
        }))
        ;
}

template <class T, class C>
void exportDragonFIR(py::module &m, const char *name)
{
    py::class_<Dragon::FIR<T,C>, Filter<T,T>, std::unique_ptr<Dragon::FIR<T,C>>>(m, name)
        .def(py::init<const std::vector<C>&>())
        .def_property_readonly("delay",
            &Dragon::FIR<T,C>::getDelay,
            "Return filter delay")
        .def_property("taps",
            &Dragon::FIR<T,C>::getTaps,
            &Dragon::FIR<T,C>::setTaps,
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

void exportFilters(py::module &m)
{
    using C = std::complex<float>;
    using F = float;

    exportFilter<C,C>(m, "FilterCC");
    exportDragonFIR<C,C>(m, "FIRCCC");
    exportDragonFIR<C,F>(m, "FIRCCF");
    exportLiquidFIR<C,C,C>(m, "LiquidFIRCCC");
    exportLiquidIIR<C,C,C>(m, "LiquidIIRCCC");

    m.def("parks_mcclellan", &Liquid::parks_mcclellan);

    m.def("kaiser", &Liquid::kaiser);

    m.def("butter_lowpass", &Liquid::butter_lowpass);

    py::class_<PMOutput>(m, "PMOutput")
        .def_readonly("h",
            &PMOutput::h,
            "Final filter coefficients")
        .def_readonly("x",
            &PMOutput::x,
            "Reference set used to generate the final filter")
        .def_readonly("iter",
            &PMOutput::iter,
            "Number of iterations that were necessary to achieve convergence")
        .def_readonly("delta",
            &PMOutput::delta,
            "The final reference error")
        .def_readonly("Q",
            &PMOutput::Q,
            "convergence parameter value")
        .def("__repr__", [](const PMOutput& self) {
            return py::str("PMOutput(h={}, x={}, iter={}, delta={}, Q={})").format(self.h, self.x, self.iter, self.delta, self.Q);
         })
        ;

    m.def("firpm",
        &Dragon::firpm,
        "Use the Remez exchange algorithm to design an equiripple filter",
        py::arg("numtaps"),
        py::arg("bands"),
        py::arg("desired"),
        py::arg("w"),
        py::arg("fs") = 2.0,
        py::arg("epsT") = 0.01,
        py::arg("Nmax") = 4);

    m.def("firpm1f",
        &Dragon::firpm1f,
        "Use the Remez exchange algorithm to design a filter with 1/f rolloff",
        py::arg("numtaps"),
        py::arg("bands"),
        py::arg("desired"),
        py::arg("w"),
        py::arg("fs") = 2.0,
        py::arg("epsT") = 0.01,
        py::arg("Nmax") = 4);

    m.def("firpm1f2",
        &Dragon::firpm1f2,
        "Use the Remez exchange algorithm to design a filter with 1/f^2 rolloff",
        py::arg("numtaps"),
        py::arg("bands"),
        py::arg("desired"),
        py::arg("w"),
        py::arg("fs") = 2.0,
        py::arg("epsT") = 0.01,
        py::arg("Nmax") = 4);

    exportWindow<C>(m, "WindowC");
}
