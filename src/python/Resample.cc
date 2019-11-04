#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "dsp/Polyphase.hh"
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

template <class T, class C>
void exportDragonPfb(py::module &m, const char *name)
{
    py::class_<Dragon::Pfb<T,C>, std::shared_ptr<Dragon::Pfb<T,C>>>(m, name)
        .def(py::init<unsigned,
                      const std::vector<C>&>())
        .def_property("nchannels",
            &Dragon::Pfb<T,C>::getNumChannels,
            &Dragon::Pfb<T,C>::setNumChannels,
            "Number of channels")
        .def_property("taps",
            &Dragon::Pfb<T,C>::getTaps,
            &Dragon::Pfb<T,C>::setTaps,
            "Prototype filter taps")
        .def_property_readonly("channel_taps",
            &Dragon::Pfb<T,C>::getChannelTaps,
            "Per-channel taps (reversed)")
        ;
}

template <class T, class C>
void exportDragonUpsampler(py::module &m, const char *name)
{
    py::class_<Dragon::Upsampler<T,C>, Dragon::Pfb<T,C>, Resampler<T,T>, std::shared_ptr<Dragon::Upsampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      const std::vector<C>&>())
        ;
}

template <class T, class C>
void exportDragonDownsampler(py::module &m, const char *name)
{
    py::class_<Dragon::Downsampler<T,C>, Dragon::Pfb<T,C>, Resampler<T,T>, std::shared_ptr<Dragon::Downsampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      const std::vector<C>&>())
        ;
}

template <class T, class C>
void exportDragonRationalResampler(py::module &m, const char *name)
{
    py::class_<Dragon::RationalResampler<T,C>, Dragon::Pfb<T,C>, Resampler<T,T>, std::shared_ptr<Dragon::RationalResampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      const std::vector<C>&>())
        .def(py::init<double,
                      const std::vector<C>&>())
        .def(py::init<double>())
        .def_property_readonly("up_rate",
            &Dragon::RationalResampler<T,C>::getUpRate,
            "Upsample rate")
        .def_property_readonly("down_rate",
            &Dragon::RationalResampler<T,C>::getDownRate,
            "Downsample rate")
        ;
}

template <class T, class C>
void exportDragonMixingRationalResampler(py::module &m, const char *name)
{
    using I = T;
    using O = T;
    using pyarray_I = py::array_t<I, py::array::c_style | py::array::forcecast>;
    using pyarray_O = py::array_t<O>;

    py::class_<Dragon::MixingRationalResampler<T,C>,
               Dragon::RationalResampler<T,C>,
               std::shared_ptr<Dragon::MixingRationalResampler<T,C>>>(m, name)
        .def(py::init<unsigned,
                      unsigned,
                      double,
                      const std::vector<C>&>())
        .def(py::init<double,
                      double,
                      const std::vector<C>&>())
        .def_property("shift",
            &Dragon::MixingRationalResampler<T,C>::getFreqShift,
            &Dragon::MixingRationalResampler<T,C>::setFreqShift,
            "Mixing frequency shift")
        .def_property_readonly("bandpass_taps",
            &Dragon::MixingRationalResampler<T,C>::getBandpassTaps,
            "Prototype bandpass filter taps")
        .def("resampleMixUp",
          [](Dragon::MixingRationalResampler<I,O> &resamp, pyarray_I sig) -> pyarray_O
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
          [](Dragon::MixingRationalResampler<I,O> &resamp, pyarray_I sig) -> pyarray_O
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

void exportResamplers(py::module &m)
{
    using C = std::complex<float>;
    using F = float;

    exportResampler<C,C>(m, "ResamplerCC");
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
}
