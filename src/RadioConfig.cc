#include "RadioConfig.hh"

RadioConfig rc;

RadioConfig::RadioConfig()
  : verbose(false)
  , debug(false)
  , amc_short_per_nslots(2)
  , amc_long_per_nslots(8)
  , timestamp_delay(100e-3)
  , max_packet_size(1500)
  , arq_ack_delay(100e-3)
  , arq_retransmission_delay(500e-3)
  , arq_max_reorder_delay(100e-3)
  , slot_modulate_time(35e-3)
  , slot_send_time(20e-3)
{
}
