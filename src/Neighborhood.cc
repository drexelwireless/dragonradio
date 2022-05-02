// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cstddef>
#include <cstring>
#include <functional>

#include "Neighborhood.hh"

std::optional<NodeId> Neighborhood::getTimeMaster(void)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::optional<NodeId>       master;

    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it->second->is_gateway && (!master || it->first < *master))
            master = it->first;
    }

    return master;
}
