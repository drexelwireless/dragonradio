// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <complex>
#include <vector>

#include <boost/functional/hash.hpp>

struct Channel {
    Channel() : fc(0.0), bw(0.0) {};
    Channel(double fc_, double bw_) : fc(fc_), bw(bw_) {};

    bool operator ==(const Channel &other) const
    {
        return fc == other.fc && bw == other.bw;
    }

    bool operator !=(const Channel &other) const
    {
        return !(*this == other);
    }

    bool operator <(const Channel &other) const
    {
        return fc < other.fc;
    }

    bool operator >(const Channel &other) const
    {
        return fc > other.fc;
    }

    bool intersects(const Channel &other) const
    {
        double start = fc - bw/2.;
        double end = fc + bw/2.;
        double other_start = other.fc - other.bw/2.;
        double other_end = other.fc + other.bw/2.;

        return (start < other_end) && (end > other_start);
    }

    /** @brief Frequency shift from center */
    double fc;

    /** @brief Bandwidth */
    double bw;
};

#if !defined(DOXYGEN)
// Doxygen complains:
// warning: Internal inconsistency: scope for class std::hash< Channel > not found!
template<>
struct std::hash<Channel> {
    size_t operator()(const Channel &chan)
    {
        std::size_t h = std::hash<double>{}(chan.fc);

        boost::hash_combine(h, std::hash<double>{}(chan.bw));
        return h;
    }
};
#endif /* !defined(DOXYGEN) */

using C = std::complex<float>;

/** @brief FIR taps */
using Taps = std::vector<C>;

/** @brief A vector of pairs of channels and taps */
using Channels = std::vector<std::pair<Channel, Taps>>;

#endif /* CHANNEL_H_ */
