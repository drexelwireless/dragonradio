// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FIRDESIGN_H_
#define FIRDESIGN_H_

#include <complex>
#include <vector>

namespace Dragon {
    PMOutput firpm(std::size_t N,
                   std::vector<double>const& f,
                   std::vector<double>const& a,
                   std::vector<double>const& w,
                   double fs = 2,
                   double epsT = 0.01,
                   int Nmax = 4);

    PMOutput firpmf(std::size_t N,
                    std::vector<double>const& f,
                    std::vector<double>const& a,
                    std::vector<double>const& w,
                    std::function<double(double, double, double, double, double, double)> g,
                    double fs = 2,
                    double epsT = 0.01,
                    int Nmax = 4);

    PMOutput firpm1f(std::size_t N,
                     std::vector<double>const& f,
                     std::vector<double>const& a,
                     std::vector<double>const& w,
                     double fs = 2,
                     double epsT = 0.01,
                     int Nmax = 4);

    PMOutput firpm1f2(std::size_t N,
                      std::vector<double>const& f,
                      std::vector<double>const& a,
                      std::vector<double>const& w,
                      double fs = 2,
                      double epsT = 0.01,
                      int Nmax = 4);
}

#endif /* FIRDESIGN_H_ */
