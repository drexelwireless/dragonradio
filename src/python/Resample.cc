#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "dsp/Resample.hh"
#include "liquid/Resample.hh"
#include "python/PyModules.hh"

template <class I, class O>
void exportResampler(py::module &m, const char *name)
{
    using pyarray_I = py::array_t<I, py::array::c_style | py::array::forcecast>;
    using pyarray_O = py::array_t<O>;

    py::class_<Resampler<I,O>, std::shared_ptr<Resampler<I,O>>>(m, name)
        .def_property_readonly("rate",
            &Resampler<I,O>::getRate,
            "Resampling rate")
        .def_property_readonly("delay",
            &Resampler<I,O>::getDelay,
            "Resampling delay")
        .def("reset",
            &Resampler<I,O>::reset,
            "Reset resampler state")
        .def("resample",
            [](Resampler<I,O> &resamp, pyarray_I sig) -> pyarray_O
            {
                auto      inbuf = sig.request();
                pyarray_O outarr(resamp.neededOut(inbuf.size));
                auto      outbuf = outarr.request();
                unsigned  nw;

                nw = resamp.resample(static_cast<I*>(inbuf.ptr),
                                     inbuf.size,
                                     static_cast<O*>(outbuf.ptr));
                assert(nw <= outbuf.size);

                outarr.resize({nw});

                return outarr;
            },
            "Resample signal")
        ;
}

template <class I, class O, class C>
void exportLiquidMSResamp(py::module &m, const char *name)
{
    py::class_<Liquid::MultiStageResampler<I,O,C>, Resampler<I,O>, std::shared_ptr<Liquid::MultiStageResampler<I,O,C>>>(m, name)
        .def(py::init<double,
                      unsigned,
                      double,
                      double,
                      unsigned>())
        ;
}

void exportResamplers(py::module &m)
{
    using C = std::complex<float>;
    using F = float;

    exportResampler<C,C>(m, "ResamplerCC");
    exportLiquidMSResamp<C,C,F>(m, "LiquidMSResampCCF");
}
