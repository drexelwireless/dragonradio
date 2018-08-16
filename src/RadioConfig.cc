#include "RadioConfig.hh"

RadioConfig rc;

RadioConfig::RadioConfig()
  : verbose(false)
  , short_per_nslots(2)
  , long_per_nslots(8)
  , timestamp_delay(100e-3)
  , max_packet_size(1500)
  , ack_delay(100e-3)
  , retransmission_delay(500e-3)
  , max_reorder_delay(100e-3)
  , slot_modulate_time(35e-3)
  , slot_send_time(20e-3)
{
}
