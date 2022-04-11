// Copyright 2018-2021 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>
#include <pybind11/chrono.h>

#include "Clock.hh"
#include "python/PyModules.hh"

void exportClock(py::module &m)
{
    py::class_<MonoClock::TimeKeeper, std::shared_ptr<MonoClock::TimeKeeper>>(m, "TimeKeeper")
      ;

    py::class_<MonoClock, std::shared_ptr<MonoClock>>(m, "MonoClock")
      .def_property("time_keeper",
          nullptr,
          [](MonoClock &self, const std::shared_ptr<MonoClock::TimeKeeper>& time_keeper) {
              MonoClock::set_time_keeper(time_keeper);
          })
      .def("reset_time_keeper",
          [](MonoClock &self) {
              MonoClock::reset_time_keeper();
          })
      ;

    py::class_<WallClock, MonoClock, std::shared_ptr<WallClock>>(m, "WallClock")
      .def_property_readonly_static("t0",
          [](py::object /* self */) {
              return WallClock::get_t0();
          })
      .def_property("offset",
          [](py::object /* self */) {
              return WallClock::get_offset();
          },
          [](py::object /* self */, WallClock::duration offset) {
              return WallClock::set_offset(offset);
          })
      .def_property("skew",
          [](py::object /* self */) {
              return WallClock::get_skew();
          },
          [](py::object /* self */, double skew) {
              return WallClock::set_skew(skew);
          })
      ;

    m.attr("clock") = std::make_shared<WallClock>();
}
