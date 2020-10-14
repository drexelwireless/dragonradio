// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

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

    /** @brief Maximum Transmission Unit (bytes) */
    unsigned mtu;

    /** @brief Snapshot collector */
    std::shared_ptr<SnapshotCollector> snapshot_collector;
};

#endif /* RADIOCONFIG_H_ */
