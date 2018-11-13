#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>

#include "IQBuffer.hh"
#include "IQCompression.hh"
#include "IQCompression/FLAC.hh"
#include "python/PyModules.hh"

class __attribute__ ((visibility("hidden"))) PyArrayEncoder : public FLACMemoryEncoder {
public:
    PyArrayEncoder()
    {
    }

    virtual ~PyArrayEncoder() = default;

    /** @brief Buffer for encoded data */
    py::array_t<char> encoded_bytes;

protected:
    size_t size(void) override
    {
        return encoded_bytes.size();
    }

    char *data(void) override
    {
        auto buf = encoded_bytes.request();

        return reinterpret_cast<char*>(buf.ptr);
    }

    void resize(size_t size) override
    {
        encoded_bytes.resize({size});
    }
};

class __attribute__ ((visibility("hidden"))) PyArrayDecoder : public FLACMemoryDecoder {
public:
    PyArrayDecoder(const char *encoded, size_t n)
        : FLACMemoryDecoder(encoded, n)
    {
    }

    virtual ~PyArrayDecoder() = default;

    /** @brief Buffer for decoded signal */
    py::array_t<fc32_t> decoded_sig;

protected:
    size_t size(void) override
    {
        return decoded_sig.size();
    }

    float *data(void) override
    {
        auto buf = decoded_sig.request();

        return reinterpret_cast<float*>(buf.ptr);
    }

    void resize(size_t size) override
    {
        decoded_sig.resize({size});
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

    m.def("compressFLAC", [](unsigned compression_level, py::array_t<fc32_t> sig) -> py::bytes {
        PyArrayEncoder encoder;
        auto           sigbuf = sig.request();

        encoder.encode(compression_level,
                       reinterpret_cast<fc32_t*>(sigbuf.ptr),
                       sig.size());

        return encoder.encoded_bytes;
    }, "compress fc32 samples using FLAC")
    ;

    m.def("decompressFLAC", [](py::array_t<char> data) -> py::array_t<fc32_t> {
        auto           buf = data.request();
        PyArrayDecoder decoder(reinterpret_cast<char*>(buf.ptr), buf.size);

        decoder.decode();

        return decoder.decoded_sig;
    }, "decompress fc32 samples using FLAC")
    ;
}
