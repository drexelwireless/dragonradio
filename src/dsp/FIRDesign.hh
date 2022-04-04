// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FIRDESIGN_H_
#define FIRDESIGN_H_

#include <complex>
#include <vector>

#include <firpm/pm.h>
#include <firpm/band.h>
#include <firpm/barycentric.h>
#include <firpm/pmmath.h>

namespace dragonradio::signal {

pm::pmoutput_t<double>
firpm(std::size_t n,
      std::vector<double>const& f,
      std::vector<double>const& a,
      std::vector<double>const& w,
      double fs = 2.0,
      double eps = 0.01,
      std::size_t nmax = 4u,
      pm::init_t strategy = pm::init_t::UNIFORM,
      std::size_t depth = 0u,
      pm::init_t rstrategy = pm::init_t::UNIFORM,
      unsigned long prec = 165ul);

pm::pmoutput_t<double>
firpm1f(std::size_t n,
        std::vector<double>const& f,
        std::vector<double>const& a,
        std::vector<double>const& w,
        double fs = 2.0,
        double eps = 0.01,
        std::size_t nmax = 4u,
        pm::init_t strategy = pm::init_t::UNIFORM,
        std::size_t depth = 0u,
        pm::init_t rstrategy = pm::init_t::UNIFORM,
        unsigned long prec = 165ul);

pm::pmoutput_t<double>
firpm1f2(std::size_t n,
         std::vector<double>const& f,
         std::vector<double>const& a,
         std::vector<double>const& w,
         double fs = 2.0,
         double eps = 0.01,
         std::size_t nmax = 4u,
         pm::init_t strategy = pm::init_t::UNIFORM,
         std::size_t depth = 0u,
         pm::init_t rstrategy = pm::init_t::UNIFORM,
         unsigned long prec = 165ul);

}

#endif /* FIRDESIGN_H_ */
