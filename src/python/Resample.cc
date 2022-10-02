// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "dsp/Polyphase.hh"
#include "dsp/Resample.hh"
#include "dsp/FDDownsampler.hh"
#include "dsp/FDResampler.hh"
#include "dsp/FDUpsampler.hh"
#include "liquid/Resample.hh"
#include "python/PyModules.hh"

using dragonradio::signal::Resampler;
using dragonradio::signal::pfb::Pfb;
using dragonradio::signal::FDDownsampler;
using dragonradio::signal::FDResampler;
using dragonradio::signal::FDUpsampler;

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

template <class I, class O>
void exportRationalResampler(py::module &m, const char *name)
{
    py::class_<dragonradio::signal::RationalResampler<I,O>,
               Resampler<I,O>,
               std::shared_ptr<dragonradio::signal::RationalResampler<I,O>>>(m, name)
        .def_property_readonly("interpolation_rate",
            &dragonradio::signal::RationalResampler<I,O>::getInterpolationRate,
            "int: Interpolation rate")
        .def_property_readonly("decimation_rate",
            &dragonradio::signal::RationalResampler<I,O>::getDecimationRate,
            "int: Decimation rate")
        ;
}

template <class I, class O, class C>
void exportLiquidMSResamp(py::module &m, const char *name)
{
    py::class_<liquid::MultiStageResampler<I,O,C>,
               Resampler<I,O>,
               std::shared_ptr<liquid::MultiStageResampler<I,O,C>>>(m, name)
        .def(py::init<double,
                      unsigned,
                      double,
                      double,
                      unsigned>(),
            "Construct a liquid-dsp mult-stage resampler",
            py::arg("rate"),
            py::arg("m"),
            py::arg("fs"),
            py::arg("As"),
            py::arg("npfb"))
        ;
}

template <class T, class C>
void exportDragonPfb(py::module &m, const char *name)
{
    py::class_<Pfb<T,C>, std::shared_ptr<Pfb<T,C>>>(m, name)
        .def_property("nchannels",
            &Pfb<T,C>::getNumChannels,
            &Pfb<T,C>::setNumChannels,
            "Number of channels")
        .def_property("taps",
            &Pfb<T,C>::getTaps,
            &Pfb<T,C>::setTaps,
            "Prototype filter taps")
        .def_property_readonly("channel_taps",
            &Pfb<T,C>::getChannelTaps,
            "Per-channel taps (reversed)")
        ;
}

template <class T, class C>
void exportDragonUpsampler(py::module &m, const char *name)
{
    py::class_<dragonradio::signal::pfb::Upsampler<T,C>,
               Pfb<T,C>,
               Resampler<T,T>,
               std::shared_ptr<dragonradio::signal::pfb::Upsampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      const std::vector<C>&>(),
            "Construct a polyphase upsampler",
            py::arg("l"),
            py::arg("taps") = std::vector<C>{1.0})
        ;
}

template <class T, class C>
void exportDragonDownsampler(py::module &m, const char *name)
{
    py::class_<dragonradio::signal::pfb::Downsampler<T,C>,
               Pfb<T,C>,
               Resampler<T,T>,
               std::shared_ptr<dragonradio::signal::pfb::Downsampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      const std::vector<C>&>(),
            "Construct a polyphase downsampler",
            py::arg("m"),
            py::arg("taps") = std::vector<C>{1.0})
        ;
}

template <class T, class C>
void exportDragonRationalResampler(py::module &m, const char *name)
{
    py::class_<dragonradio::signal::pfb::RationalResampler<T,C>,
               Pfb<T,C>,
               dragonradio::signal::RationalResampler<T,T>,
               std::shared_ptr<dragonradio::signal::pfb::RationalResampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      const std::vector<C>&>(),
            "Construct a polyphase rational resampler",
            py::arg("l"),
            py::arg("m"),
            py::arg("taps") = std::vector<C>{1.0})
        ;
}

template <class T, class C>
void exportDragonMixingRationalResampler(py::module &m, const char *name)
{
    using I = T;
    using O = T;
    using pyarray_I = py::array_t<I, py::array::c_style | py::array::forcecast>;
    using pyarray_O = py::array_t<O>;

    py::class_<dragonradio::signal::pfb::MixingRationalResampler<T,C>,
               dragonradio::signal::pfb::RationalResampler<T,C>,
               std::shared_ptr<dragonradio::signal::pfb::MixingRationalResampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      double,
                      const std::vector<C>&>(),
            "Construct a polyphase mixing rational resampler",
            py::arg("l"),
            py::arg("m"),
            py::arg("theta"),
            py::arg("taps") = std::vector<C>{1.0})
        .def_property("theta",
            &dragonradio::signal::pfb::MixingRationalResampler<T,C>::getTheta,
            &dragonradio::signal::pfb::MixingRationalResampler<T,C>::setTheta,
            "float: Mixing frequency shift (normalized frequency)")
        .def_property_readonly("bandpass_taps",
            &dragonradio::signal::pfb::MixingRationalResampler<T,C>::getBandpassTaps,
            "Prototype bandpass filter taps")
        .def("resampleMixUp",
          [](dragonradio::signal::pfb::MixingRationalResampler<I,O> &resamp, pyarray_I sig) -> pyarray_O
          {
              auto      inbuf = sig.request();
              pyarray_O outarr(resamp.neededOut(inbuf.size));
              auto      outbuf = outarr.request();
              unsigned  nw;

              nw = resamp.resampleMixUp(static_cast<I*>(inbuf.ptr),
                                        inbuf.size,
                                        static_cast<O*>(outbuf.ptr));
              assert(nw <= outbuf.size);

              outarr.resize({nw});

              return outarr;
          },
          "Mix up and resample signal")
        .def("resampleMixDown",
          [](dragonradio::signal::pfb::MixingRationalResampler<I,O> &resamp, pyarray_I sig) -> pyarray_O
          {
              auto      inbuf = sig.request();
              pyarray_O outarr(resamp.neededOut(inbuf.size));
              auto      outbuf = outarr.request();
              unsigned  nw;

              nw = resamp.resampleMixDown(static_cast<I*>(inbuf.ptr),
                                          inbuf.size,
                                          static_cast<O*>(outbuf.ptr));
              assert(nw <= outbuf.size);

              outarr.resize({nw});

              return outarr;
          },
          "Resample signal and mix down")
        ;
}

template <class T>
void exportDragonFDUpsampler(py::module &m, const char *name)
{
    py::class_<FDUpsampler<T>,
               dragonradio::signal::Resampler<T,T>,
               std::shared_ptr<FDUpsampler<T>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      double>(),
            "Construct a frequency domain upsampler",
            py::arg("X"),
            py::arg("I"),
            py::arg("theta"))
        .def_readonly_static("P",
            &FDUpsampler<T>::P,
            "int: Maximum prototype filter length.")
        .def_readonly_static("V",
            &FDUpsampler<T>::V,
            "int: Overlap factor.")
        .def_readonly_static("N",
            &FDUpsampler<T>::N,
            "int: FFT size.")
        .def_readonly_static("L",
            &FDUpsampler<T>::L,
            "int: Samples consumer per input block.")
        ;
}

template <class T>
void exportDragonFDDownsampler(py::module &m, const char *name)
{
    py::class_<FDDownsampler<T>,
               dragonradio::signal::Resampler<T,T>,
               std::shared_ptr<FDDownsampler<T>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      double,
                      const std::vector<T>&>(),
            "Construct a frequency domain downsampler",
            py::arg("X"),
            py::arg("D"),
            py::arg("theta"),
            py::arg("taps") = std::vector<C>{static_cast<C>(1.0)})
        .def_readonly_static("P",
            &FDUpsampler<T>::P,
            "int: Maximum prototype filter length.")
        .def_readonly_static("V",
            &FDDownsampler<T>::V,
            "int: Overlap factor.")
        .def_readonly_static("N",
            &FDDownsampler<T>::N,
            "int: FFT size.")
        .def_readonly_static("O",
            &FDDownsampler<T>::O,
            "int: overlap size.")
        .def_readonly_static("L",
            &FDDownsampler<T>::L,
            "int: Samples consumer per input block.")
        ;
}

template <class T>
void exportDragonFDResampler(py::module &m, const char *name)
{
    py::class_<FDResampler<T>,
               dragonradio::signal::RationalResampler<T,T>,
               std::shared_ptr<FDResampler<T>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      unsigned,
                      double,
                      const std::vector<T>&>(),
            "Construct a frequency domain resampler",
            py::arg("I"),
            py::arg("D"),
            py::arg("X") = 1,
            py::arg("theta") = 0.0,
            py::arg("taps") = std::vector<C>{static_cast<C>(1.0)})
        .def_property("exact",
            &FDResampler<T>::getExact,
            &FDResampler<T>::setExact,
            "bool: If True, then resampling is exact")
        .def_property("parallel",
            &FDResampler<T>::getParallelizable,
            &FDResampler<T>::setParallelizable,
            "bool: If True, then resampling is parallelizable")
        .def_readonly_static("P",
            &FDResampler<T>::P,
            "int: Maximum prototype filter length.")
        .def_readonly_static("V",
            &FDResampler<T>::V,
            "int: Overlap factor.")
        .def_readonly_static("N",
            &FDResampler<T>::N,
            "int: FFT size.")
        .def_readonly_static("O",
            &FDResampler<T>::O,
            "int: overlap size.")
        .def_readonly_static("L",
            &FDResampler<T>::L,
            "int: Samples consumer per input block.")
        ;
}

void exportResamplers(py::module &m)
{
    using C = std::complex<float>;
    using F = float;

    exportResampler<C,C>(m, "ResamplerCC");
    exportRationalResampler<C,C>(m, "RationalResamplerCC");
    exportLiquidMSResamp<C,C,F>(m, "LiquidMSResampCCF");

    exportDragonPfb<C,F>(m, "PfbCCF");
    exportDragonPfb<C,C>(m, "PfbCCC");

    exportDragonUpsampler<C,F>(m, "UpsamplerCCF");
    exportDragonUpsampler<C,C>(m, "UpsamplerCCC");

    exportDragonDownsampler<C,F>(m, "DownsamplerCCF");
    exportDragonDownsampler<C,C>(m, "DownsamplerCCC");

    exportDragonRationalResampler<C,F>(m, "RationalResamplerCCF");
    exportDragonRationalResampler<C,C>(m, "RationalResamplerCCC");

    exportDragonMixingRationalResampler<C,C>(m, "MixingRationalResamplerCCC");

    exportDragonFDUpsampler<C>(m, "FDUpsamplerCCC");
    exportDragonFDDownsampler<C>(m, "FDDownsamplerCCC");
    exportDragonFDResampler<C>(m, "FDResamplerCCC");
}
