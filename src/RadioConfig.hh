#ifndef RADIOCONFIG_H_
#define RADIOCONFIG_H_

#include <memory>

#include <liquid/liquid.h>

class RadioConfig;

/** @brief The global radio config. */
extern std::shared_ptr<RadioConfig> rc;

class RadioConfig {
public:
    RadioConfig();
    ~RadioConfig();

    /** @brief Give verbose debug messages on the console */
    bool verbose;

    /** @brief Flag indicating whether or not this node is the gateway */
    bool is_gateway;

    /** @brief Default soft TX gain (dB) */
    double soft_tx_gain;

    /** @brief Default modulation scheme */
    modulation_scheme ms;

    /** @brief Default data validity check */
    crc_scheme check;

    /** @brief Default inner FEC */
    fec_scheme fec0;

    /** @brief Default outer FEC */
    fec_scheme fec1;
};

#endif /* RADIOCONFIG_H_ */
