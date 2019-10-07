#include "RadioConfig.hh"

RadioConfig rc;

RadioConfig::RadioConfig()
  : verbose(false)
  , debug(false)
  , log_invalid_headers(false)
  , mtu(1500)
  , verbose_packet_trace(false)
{
}
