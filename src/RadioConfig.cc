#include "RadioConfig.hh"

std::shared_ptr<RadioConfig> rc;

RadioConfig::RadioConfig() :
    verbose(false),
    is_gateway(false)
{
}

RadioConfig::~RadioConfig()
{
}
