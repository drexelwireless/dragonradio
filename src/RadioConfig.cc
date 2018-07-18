#include "RadioConfig.hh"

RadioConfig rc;

RadioConfig::RadioConfig()
  : verbose(false)
  , is_gateway(false)
  , short_per_npackets(50)
  , long_per_npackets(200)
  , timestamp_delay(100e-3)
  , max_packet_size(1500)
  , ack_delay(100e-3)
  , retransmission_delay(500e-3)
{
}
