// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef NODE_HH_
#define NODE_HH_

#include <math.h>

struct GPSLocation {
    GPSLocation() : lat(0.0), lon(0.0), alt(0.0), timestamp(0.0)
    {
    }

    /** @brief Latitude */
    double lat;

    /** @brief Longitude */
    double lon;

    /** @brief Altitude */
    double alt;

    /** @brief Timestamp of last update */
    double timestamp;
};

typedef uint8_t NodeId;

const NodeId kNodeBroadcast = 255;

struct Node {
    explicit Node(NodeId id)
      : id(id)
      , is_gateway(false)
      , emcon(false)
      , unreachable(false)
      , g(1.0)
    {
    }

    Node() = delete;
    Node(const Node &) = delete;

    ~Node() = default;

    /** @brief Node ID */
    const NodeId id;

    /** @brief Location */
    GPSLocation loc;

    /** @brief Flag indicating whether or not this node is the gateway */
    bool is_gateway;

    /** @brief Flag indicating whether or not this node is subject to emissions control */
    bool emcon;

    /** @brief Flag indicating whether or not this node is unreachable */
    bool unreachable;

    /** @brief Multiplicative TX gain as measured against 0 dBFS. */
    float g;

    /** @brief Set soft TX gain.
     * @param dB The soft gain (dBFS).
     */
    void setSoftTXGain(float dB)
    {
        g = powf(10.0f, dB/20.0f);
    }

    /** @brief Get soft TX gain (dBFS). */
    float getSoftTXGain(void) const
    {
        return 20.0*logf(g)/logf(10.0);
    }
};

#endif /* NODE_HH_ */
