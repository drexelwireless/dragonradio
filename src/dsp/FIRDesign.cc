// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <firpm/pm.h>

#include "FIRDesign.hh"

namespace dragonradio::signal {

PMOutput firpm(std::size_t N,
               std::vector<double>const& f0,
               std::vector<double>const& a,
               std::vector<double>const& w,
               double fs,
               double epsT,
               int Nmax)
{
    std::vector<double> f(f0);

    // Rescale based on fs
    if (fs != 2.0)
        std::for_each(f.begin(), f.end(), [k = 2.0/fs](double &x) { x *= k; });

    return ::firpm(N-1, f, a, w, epsT, Nmax);
}

PMOutput firpmf(std::size_t N,
                std::vector<double>const& f0,
                std::vector<double>const& a,
                std::vector<double>const& w,
                std::function<double(double, double, double, double, double, double)> g,
                double fs,
                double eps,
                int Nmax)
{
    std::vector<double> f(f0);
    std::vector<double> h;
    std::size_t         n = N-1;

    // Rescale based on fs
    if (fs != 2.0)
        std::for_each(f.begin(), f.end(), [k = 2.0/fs](double &x) { x *= k; });

    if (n % 2 != 0)
        throw std::range_error("Number of taps must be odd.");

    // The code below is a modified copy of the implementation of firpm. We
    // change the weight function to get 1/f rolloff in the stopband.

    std::size_t degree = n / 2u;
    // TODO: error checking code
    std::vector<Band> freqBands(w.size());
    std::vector<Band> chebyBands;
    for(std::size_t i{0u}; i < freqBands.size(); ++i)
    {
        freqBands[i].start = M_PI * f[2u * i];
        freqBands[i].stop  = M_PI * f[2u * i + 1u];
        freqBands[i].space = BandSpace::FREQ;
        freqBands[i].amplitude = [=](BandSpace bSpace, double x) -> double
        {
            if (a[2u * i] != a[2u * i + 1u]) {
                if(bSpace == BandSpace::CHEBY)
                    x = acosl(x);
                return ((x - freqBands[i].start) * a[2u * i + 1u] -
                        (x - freqBands[i].stop) * a[2u * i]) /
                        (freqBands[i].stop - freqBands[i].start);
            }
            return a[2u * i];
        };
        freqBands[i].weight = [=](BandSpace bSpace, double x) -> double
        {
            return g(acosl(x),
                     f[2u * i], f[2u * i + 1],
                     a[2u * i], a[2u * i + 1],
                     w[i]);
        };
    }

    std::vector<double> omega(degree + 2u);
    std::vector<double> x(degree + 2u);

    initUniformExtremas(omega, freqBands);
    applyCos(x, omega);
    bandConversion(chebyBands, freqBands, ConversionDirection::FROMFREQ);

    PMOutput output = exchange(x, chebyBands, eps, Nmax);

    h.resize(n + 1u);
    h[degree] = output.h[0];
    for(std::size_t i{0u}; i < degree; ++i)
        h[i] = h[n - i] = output.h[degree - i] / 2u;
    output.h = h;

    return output;
}

PMOutput firpm1f(std::size_t N,
                 std::vector<double>const& f,
                 std::vector<double>const& a,
                 std::vector<double>const& w,
                 double fs,
                 double eps,
                 int Nmax)
{
    auto g = [](double f, double f0, double f1, double a0, double a1, double w)
             {
                 if (a0 > 0.001) {
                     // Passband
                     return w;
                 } else {
                     // Stopband
                     double k = f/(M_PI * f0);

                     return w*k;
                 }
             };

    return firpmf(N, f, a, w, g, fs, eps, Nmax);
}

PMOutput firpm1f2(std::size_t N,
                  std::vector<double>const& f,
                  std::vector<double>const& a,
                  std::vector<double>const& w,
                  double fs,
                  double eps,
                  int Nmax)
{
    auto g = [](double f, double f0, double f1, double a0, double a1, double w)
             {
                 if (a0 > 0.001) {
                     // Passband
                     return w;
                 } else {
                     // Stopband
                     double k = f/(M_PI * f0);

                     return w*k*k;
                 }
             };

    return firpmf(N, f, a, w, g, fs, eps, Nmax);
}

}
