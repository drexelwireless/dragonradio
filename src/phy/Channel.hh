#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <complex>
#include <vector>

struct Channel {
    Channel() : fc(0.0), bw(0.0) {};
    Channel(double fc_, double bw_) : fc(fc_), bw(bw_) {};

    /** @brief Frequency shift from center */
    double fc;

    /** @brief Bandwidth */
    double bw;
};

using C = std::complex<float>;

/** @brief A vector of pairs of channels and taps */
using Channels = std::vector<std::pair<Channel, std::vector<C>>>;

#endif /* CHANNEL_H_ */
