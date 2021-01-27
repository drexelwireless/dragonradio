// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "RadioConfig.hh"

RadioConfig rc;

RadioConfig::RadioConfig()
  : node_id(0)
  , log_invalid_headers(false)
  , mtu(1500)
{
}
