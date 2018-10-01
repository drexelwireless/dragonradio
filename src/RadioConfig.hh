#ifndef RADIOCONFIG_H_
#define RADIOCONFIG_H_

#include <complex>
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

    /** @brief Output verbose messages to the console */
    bool verbose;

    /** @brief Output debug messages to the console */
    bool debug;

    /** @brief Number of slots worth of packets we use to calculate short-term PER  */
    unsigned amc_short_per_nslots;

    /** @brief Number of slots worth of packets we use to calculate long-term PER  */
    unsigned amc_long_per_nslots;

    /** @brief Timestamp delay, in seconds */
    double timestamp_delay;

    /** @brief Maximum Transmission Unit (bytes) */
    unsigned mtu;

    /** @brief ACK delay in seconds */
    double arq_ack_delay;

    /** @brief Retransmission delay in seconds */
    double arq_retransmission_delay;

    /** @brief (Estimated) maximum packet reordering delay in seconds */
    double arq_max_reorder_delay;

    /** @brief Time needed to modulate a slot's worth of data. */
    double slot_modulate_time;

    /** @brief Time needed to send a slot's worth of data. */
    double slot_send_time;
};

#endif /* RADIOCONFIG_H_ */
