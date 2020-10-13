#include "PHY.hh"

uint8_t PHY::team_ = 0;

NodeId PHY::node_id_ = 0;

bool PHY::log_invalid_headers_ = false;

std::shared_ptr<SnapshotCollector> PHY::snapshot_collector_;
