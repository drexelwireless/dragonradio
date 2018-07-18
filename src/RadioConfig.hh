#ifndef RADIOCONFIG_H_
#define RADIOCONFIG_H_

#include <memory>

#include <liquid/liquid.h>

class RadioConfig;

/** @brief The global radio config. */
extern RadioConfig rc;

class RadioConfig {
public:
    RadioConfig();
    RadioConfig(const RadioConfig&) = default;
    RadioConfig(RadioConfig&&) = default;

    ~RadioConfig() = default;

    RadioConfig& operator=(const RadioConfig&) = default;
    RadioConfig& operator=(RadioConfig&&) = default;

    /** @brief Give verbose debug messages on the console */
    bool verbose;

    /** @brief Flag indicating whether or not this node is the gateway */
    bool is_gateway;

    /** @brief Number of packets we use to calculate short-term PER  */
    unsigned short_per_npackets;

    /** @brief Number of packets we use to calculate long-term PER  */
    unsigned long_per_npackets;

    /** @brief Timestamp delay, in seconds */
    double timestamp_delay;

    /** @brief Maximum size of a packet, in bytes */
    unsigned max_packet_size;

    /** @brief ACK delay in seconds */
    double ack_delay;

    /** @brief Retransmission delay in seconds */
    double retransmission_delay;
};

#endif /* RADIOCONFIG_H_ */
