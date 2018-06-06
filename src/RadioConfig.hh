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
};

#endif /* RADIOCONFIG_H_ */
