#include "RadioConfig.hh"

std::shared_ptr<RadioConfig> rc;

RadioConfig::RadioConfig() :
    verbose(false),
    soft_txgain(-12.0f),
    ms(LIQUID_MODEM_QPSK),
    check(LIQUID_CRC_32),
    fec0(LIQUID_FEC_CONV_V29),
    fec1(LIQUID_FEC_RS_M8)
{
}

RadioConfig::~RadioConfig()
{
}
