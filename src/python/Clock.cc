#include <pybind11/pybind11.h>

#include "Clock.hh"
#include "python/PyModules.hh"

void exportClock(py::module &m)
{
    // Export class Clock::time_point to Python
    py::class_<Clock::time_point, std::shared_ptr<Clock::time_point>>(m, "TimePoint")
        .def_property_readonly("full_secs",
            [](const Clock::time_point t) {
                return t.get_full_secs();
            },
            "Full seconds")
        .def_property_readonly("frac_secs",
            [](const Clock::time_point t) {
                return t.get_frac_secs();
            },
            "Fractional seconds")
        .def_property_readonly("secs",
            [](const Clock::time_point t) {
                return t.get_real_secs();
            },
            "Seconds")
        .def("__repr__",
            [](const Clock::time_point& self) {
                return py::str("TimePoint(full_secs={}, frac_secs={})").format(self.get_full_secs(), self.get_frac_secs());
            });
        ;
}
