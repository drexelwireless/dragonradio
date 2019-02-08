#ifndef CHANNEL_H_
#define CHANNEL_H_

#include <vector>

struct Channel {
    Channel() : fc(0.0), bw(0.0) {};
    Channel(double fc_, double bw_) : fc(fc_), bw(bw_) {};

    /** @brief Frequency shift from center */
    double fc;

    /** @brief Bandwidth */
    double bw;
};

using Channels = std::vector<Channel>;

#endif /* CHANNEL_H_ */
