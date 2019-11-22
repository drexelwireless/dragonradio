#ifndef RADIOCONFIG_H_
#define RADIOCONFIG_H_

#include <complex>
#include <memory>

#include "Header.hh"

class RadioConfig;

class SnapshotCollector;

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

    /** @brief The current node's ID */
    NodeId node_id;

    /** @brief Output verbose messages to the console */
    bool verbose;

    /** @brief Output debug messages to the console */
    bool debug;

    /** @brief Log invalid headers? */
    bool log_invalid_headers;

    /** @brief Maximum Transmission Unit (bytes) */
    unsigned mtu;

    /** @brief Display packets written to tun/tap device? */
    bool verbose_packet_trace;

    /** @brief Snapshot collector */
    std::shared_ptr<SnapshotCollector> snapshot_collector;
};

#endif /* RADIOCONFIG_H_ */
