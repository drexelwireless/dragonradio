#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "IQBuffer.hh"
#include "python/PyModules.hh"

using fc32 = std::complex<float>;

void exportIQBuffer(py::module &m)
{
    // Export class IQBuf to Python
    py::class_<IQBuf, std::shared_ptr<IQBuf>>(m, "IQBuf")
        .def(py::init([]() {
                return std::make_shared<IQBuf>(0);
            }))
        .def(py::init([](py::array_t<fc32> data) {
                auto buf = data.request();

                return std::make_shared<IQBuf>(reinterpret_cast<fc32*>(buf.ptr), buf.size);
            }))
#if !defined(NOUHD)
        .def_readwrite("timestamp",
            &IQBuf::timestamp,
            "IQ buffer timestamp in seconds")
#endif /* !defined(NOUHD) */
        .def_readwrite("fc",
            &IQBuf::fc,
            "Sample senter frequency")
        .def_readwrite("fs",
            &IQBuf::fs,
            "Sample rate")
        .def_readwrite("delay",
            &IQBuf::delay,
            "Signal delay")
        .def_property("data",
            [](const IQBuf &iqbuf) {
                return py::array_t<fc32>(iqbuf.size(), iqbuf.data());
            },
            [](IQBuf &iqbuf, py::array_t<fc32> data) {
                auto         buf = data.request();
                buffer<fc32> copy(reinterpret_cast<fc32*>(buf.ptr), buf.size);

                static_cast<buffer<fc32>&>(iqbuf) = copy;
            },
            "IQ data")
        .def("__repr__",
            [](const IQBuf& self) {
#if defined(NOUHD)
                return py::str("IQBuf(fc={:g}, fs={:g})").format(self.fc, self.fs);
#else /* !defined(NOUHD) */
                return py::str("IQBuf(timestamp={}, fc={:g}, fs={:g})").format(self.timestamp, self.fc, self.fs);
#endif /* !defined(NOUHD) */
             })
        ;
}
