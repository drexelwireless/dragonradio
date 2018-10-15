#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "dsp/Resample.hh"
#include "liquid/Resample.hh"
#include "python/PyModules.hh"

void exportResamplers(py::module &m)
{
    // Export class Resampler to Python
    py::class_<Resampler, std::shared_ptr<Resampler>>(m, "Resampler")
        .def_property_readonly("rate", &Resampler::getRate, "Resampling rate")
        .def_property_readonly("delay", &Resampler::getDelay, "Resampling delay")
        .def("reset", &Resampler::reset, "reset resampler state")
        .def("resample", [](Resampler &resamp, py::array_t<std::complex<float>> sig) -> py::array_t<std::complex<float>> {
            auto inbuf = sig.request();

            py::array_t<std::complex<float>> outarr(1 + 2*resamp.getRate()*inbuf.size);
            auto                             outbuf = outarr.request();
            unsigned                         nw;

            nw = resamp.resample(static_cast<std::complex<float>*>(inbuf.ptr),
                                 inbuf.size,
                                 static_cast<std::complex<float>*>(outbuf.ptr));
            assert(nw <= outbuf.size);

            outarr.resize({nw});

            return outarr;
        }, "resample signal")
        ;

    // Export class MultiStageResampler to Python
    py::class_<Liquid::MultiStageResampler, Resampler, std::shared_ptr<Liquid::MultiStageResampler>>(m, "MultiStageResampler")
        .def(py::init<double,
                      unsigned,
                      double,
                      double,
                      unsigned>())
            ;
}
