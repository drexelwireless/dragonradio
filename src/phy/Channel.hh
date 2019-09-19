#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <complex>
#include <vector>

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

    /** @brief Frequency shift from center */
    double fc;

    /** @brief Bandwidth */
    double bw;
};

using C = std::complex<float>;

/** @brief FIR taps */
using Taps = std::vector<C>;

/** @brief A vector of pairs of channels and taps */
using Channels = std::vector<std::pair<Channel, Taps>>;

#endif /* CHANNEL_H_ */
