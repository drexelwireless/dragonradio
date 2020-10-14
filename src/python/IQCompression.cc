// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>

#include "IQBuffer.hh"
#include "IQCompression.hh"
#include "IQCompression/FLAC.hh"
#include "python/PyModules.hh"

#if defined(DOXYGEN)
#define HIDDEN
#else /* !DOXYGEN */
#define HIDDEN __attribute__ ((visibility("hidden")))
#endif /* !DOXYGEN */

class HIDDEN PyArrayEncoder : public FLACMemoryEncoder {
public:
    PyArrayEncoder() = default;

    virtual ~PyArrayEncoder() = default;

    py::array_t<char> encoded;

protected:
    size_t size(void) override
    {
        return encoded.size();
    }

    char *data(void) override
    {
        auto buf = encoded.request();

        return reinterpret_cast<char*>(buf.ptr);
    }

    void resize(size_t size) override
    {
        encoded.resize({size});
    }
};

class HIDDEN PyArrayDecoder : public FLACMemoryDecoder {
public:
    PyArrayDecoder() = default;

    virtual ~PyArrayDecoder() = default;

    py::array_t<fc32_t> decoded;

protected:
    size_t size(void) override
    {
        return decoded.size();
    }

    fc32_t *data(void) override
    {
        auto buf = decoded.request();

        return reinterpret_cast<fc32_t*>(buf.ptr);
    }

    void resize(size_t size) override
    {
        decoded.resize({size});
    }
};

void exportIQCompression(py::module &m)
{
    m.def("convert2sc16", [](py::array_t<fc32_t> in) -> py::array_t<int16_t> {
        auto inbuf = in.request();

        py::array_t<int16_t> outarr(2*inbuf.size);
        auto                 outbuf = outarr.request();

        convert2sc16(static_cast<fc32_t*>(inbuf.ptr),
                     static_cast<sc16_t*>(outbuf.ptr),
                     inbuf.size);

        return outarr;
    }, "convert fc32 buffer to a sc16 buffer")
    ;

    m.def("convert2fc32", [](py::array_t<int16_t> in) -> py::array_t<fc32_t> {
        auto inbuf = in.request();

        py::array_t<fc32_t> outarr(inbuf.size/2);
        auto                outbuf = outarr.request();

        convert2fc32(static_cast<sc16_t*>(inbuf.ptr),
                     static_cast<fc32_t*>(outbuf.ptr),
                     inbuf.size/2);

        return outarr;
    }, "convert sc16 buffer to a fc32 buffer")
    ;

    m.def("compressIQData", [](py::array_t<fc32_t> sig) -> py::bytes {
        PyArrayEncoder encoder;
        auto           sigbuf = sig.request();

        encoder.encode(reinterpret_cast<fc32_t*>(sigbuf.ptr), sig.size());
        return std::move(encoder.encoded);
    }, "compress fc32 samples")
    ;

    m.def("decompressIQData", [](py::array_t<char> data) -> py::array_t<fc32_t> {
        PyArrayDecoder decoder;
        auto           buf = data.request();

        decoder.decode(reinterpret_cast<char*>(buf.ptr), buf.size);
        return std::move(decoder.decoded);
    }, "decompress fc32 samples")
    ;
}
