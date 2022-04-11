// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

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
        .def_readwrite("timestamp",
            &IQBuf::timestamp,
            "IQ buffer timestamp in seconds")
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
            [](const std::shared_ptr<IQBuf> &iqbuf) {
                if (iqbuf->complete)
                    return py::array_t<fc32>(iqbuf->size(), iqbuf->data(), sharedptr_capsule(iqbuf));
                else
                    return py::array_t<fc32>();
            },
            [](IQBuf &iqbuf, py::array_t<fc32, py::array::c_style | py::array::forcecast> data) {
                auto         buf = data.request();
                buffer<fc32> copy(reinterpret_cast<fc32*>(buf.ptr), buf.size);

                static_cast<buffer<fc32>&>(iqbuf) = std::move(copy);
            },
            "IQ data")
        .def("__getitem__",
            [](const IQBuf &self, size_t i) {
                if (i >= self.size())
                    throw py::index_error();
                return self[i];
            })
        .def("__setitem__",
            [](IQBuf &self, size_t i, fc32 v) {
                if (i >= self.size())
                    throw py::index_error();
                self[i] = v;
            })
        .def("__len__",
            [](const IQBuf &self) -> size_t
            {
                return self.size();
            })
        .def("__repr__",
            [](const IQBuf& self) {
                return py::str("IQBuf(timestamp={}, fc={:g}, fs={:g})").format(self.timestamp, self.fc, self.fs);
             })
        ;
}
