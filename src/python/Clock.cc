// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "Clock.hh"
#include "python/PyModules.hh"

template <class T>
pybind11::class_<T, std::shared_ptr<T>>
exportTimePoint(py::module &m, const char *name)
{
    return py::class_<T, std::shared_ptr<T>>(m, name)
        .def(py::init())
        .def(py::init<double>())
        .def(py::init<int64_t>())
        .def(py::init<int64_t, double>())
        .def_property_readonly("full_secs",
            [](const T &t) {
                return t.get_full_secs();
            },
            "Full seconds")
        .def_property_readonly("frac_secs",
            [](const T &t) {
                return t.get_frac_secs();
            },
            "Fractional seconds")
        .def_property_readonly("secs",
            [](const T &t) {
                return t.get_real_secs();
            },
            "Seconds")
        .def(py::self + py::self)
        .def(py::self + double())
        .def(py::self - py::self)
        .def(py::self - double())
        .def(py::self > py::self)
        .def(py::self < py::self)
        .def("__repr__",
            [name](const T &self) {
                return py::str("{}(full_secs={}, frac_secs={})").format(name, self.get_full_secs(), self.get_frac_secs());
            })
        ;
}

void exportClock(py::module &m)
{
    exportTimePoint<WallClock::time_point>(m, "WallTimePoint")
      .def_property_readonly("mono_time",
          [](WallClock::time_point &t) {
              return WallClock::to_mono_time(t);
          });

    exportTimePoint<MonoClock::time_point>(m, "MonoTimePoint")
      .def_property_readonly("wall_time",
          [](MonoClock::time_point &t) {
              return WallClock::to_wall_time(t);
          });

    py::class_<WallClock, std::shared_ptr<WallClock>>(m, "WallClock")
      .def_property_readonly("t0",
          [](WallClock &clock) {
              return clock.getTimeZero();
          })
      .def_property("offset",
          &WallClock::getTimeOffset,
          &WallClock::setTimeOffset)
      .def_property("skew",
          &WallClock::getSkew,
          &WallClock::setSkew)
      ;

    m.attr("clock") = std::make_shared<WallClock>();
}
