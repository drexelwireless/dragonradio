// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "FIRDesign.hh"

namespace dragonradio::signal {

pm::pmoutput_t<double>
firpm(std::size_t n0,
      std::vector<double>const& f0,
      std::vector<double>const& a,
      std::vector<double>const& w,
      double fs,
      double eps,
      std::size_t nmax,
      pm::init_t strategy,
      std::size_t depth,
      pm::init_t rstrategy,
      unsigned long prec)
{
    std::vector<double> f(f0);
    std::size_t         n = n0-1;

    // Rescale based on fs
    if (fs != 2.0)
        std::for_each(f.begin(), f.end(), [k = 2.0/fs](double &x) { x *= k; });

    return pm::firpm(n, f, a, w, eps, nmax, strategy, depth, rstrategy, prec);
}

pm::pmoutput_t<double>
firpm1f(std::size_t n0,
        std::vector<double>const& f0,
        std::vector<double>const& a,
        std::vector<double>const& w,
        double fs,
        double eps,
        std::size_t nmax,
        pm::init_t strategy,
        std::size_t depth,
        pm::init_t rstrategy,
        unsigned long prec)
{
    std::vector<double> f(f0);
    std::size_t         n = n0-1;

    // Rescale based on fs
    if (fs != 2.0)
        std::for_each(f.begin(), f.end(), [k = 2.0/fs](double &x) { x *= k; });

    auto frf = [](double f, double f0, double f1, double a0, double a1, double w)
               {
                   if (a0 > 0.001) {
                       // Passband
                       return w;
                   } else {
                       // Stopband
                       double k = f/f0;

                       return w*k;
                   }
               };

    return pm::firpmfrf<double>(n, f, a, w, frf, eps, nmax, strategy, depth, rstrategy, prec);
}

pm::pmoutput_t<double>
firpm1f2(std::size_t n0,
         std::vector<double>const& f0,
         std::vector<double>const& a,
         std::vector<double>const& w,
         double fs,
         double eps,
         std::size_t nmax,
         pm::init_t strategy,
         std::size_t depth,
         pm::init_t rstrategy,
         unsigned long prec)
{
    std::vector<double> f(f0);
    std::size_t         n = n0-1;

    // Rescale based on fs
    if (fs != 2.0)
        std::for_each(f.begin(), f.end(), [k = 2.0/fs](double &x) { x *= k; });

    auto frf = [](double f, double f0, double f1, double a0, double a1, double w)
               {
                   if (a0 > 0.001) {
                       // Passband
                       return w;
                   } else {
                       // Stopband
                       double k = f/f0;

                       return w*k*k;
                   }
               };

    return pm::firpmfrf<double>(n, f, a, w, frf, eps, nmax, strategy, depth, rstrategy, prec);
}

}
