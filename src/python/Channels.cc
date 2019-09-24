#include <pybind11/pybind11.h>
#include <pybind11/complex.h>
#include <pybind11/numpy.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>

#include "python/PyModules.hh"

void exportChannels(py::module &m)
{
    // Export class Channel to Python
    py::class_<Channel, std::shared_ptr<Channel>>(m, "Channel")
        .def(py::init<>())
        .def(py::init<double, double>())
        .def_readwrite("fc",
            &Channel::fc,
           "Frequency shift from center")
        .def_readwrite("bw",
            &Channel::bw,
            "Bandwidth")
        .def("intersects",
            &Channel::intersects,
            "Return true if channels intersect, false otherwise")
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self < py::self)
        .def(py::self > py::self)
        .def(hash(py::self))
        .def("__repr__", [](const Channel& self) {
            return py::str("Channel(fc={}, bw={})").format(self.fc, self.bw);
         })
        .def(py::pickle(
            [](const Channel &self) {
                return py::make_tuple(self.fc, self.bw);
            },
            [](py::tuple t) {
                if (t.size() != 2)
                    throw std::runtime_error("Invalid state!");

                return Channel(t[0].cast<double>(), t[1].cast<double>());
            }))
        ;

    // Export vector of channels/tap pairs
    py::bind_vector<Channels>(m, "Channels");
}
