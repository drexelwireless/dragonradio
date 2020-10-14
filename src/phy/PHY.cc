#include "PHY.hh"

NodeId PHY::node_id_ = 0;

bool PHY::log_invalid_headers_ = false;

std::shared_ptr<SnapshotCollector> PHY::snapshot_collector_;
