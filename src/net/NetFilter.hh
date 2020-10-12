// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef NETFILTER_HH_
#define NETFILTER_HH_

#include <functional>

#include "net/Net.hh"
#include "net/Processor.hh"

using namespace std::placeholders;

class NetFilter : public Processor<std::shared_ptr<NetPacket>>
{
public:
    NetFilter(std::shared_ptr<Net> net,
              in_addr_t int_net,
              in_addr_t int_netmask,
              in_addr_t int_broadcast,
              in_addr_t ext_net,
              in_addr_t ext_netmask,
              in_addr_t ext_broadcast)
      : net_(net)
      , int_net_(int_net)
      , int_netmask_(int_netmask)
      , int_broadcast_(int_broadcast)
      , ext_net_(ext_net)
      , ext_netmask_(ext_netmask)
      , ext_broadcast_(ext_broadcast)
    {
    }

    virtual ~NetFilter() = default;

protected:
    bool process(std::shared_ptr<NetPacket>& pkt) override;

private:
    /** @brief The Net we use to filter packets */
    std::shared_ptr<Net> net_;

    /** @brief Internal IP network */
    in_addr_t int_net_;

    /** @brief Internal IP network mask */
    in_addr_t int_netmask_;

    /** @brief Internal IP broadcast address */
    in_addr_t int_broadcast_;

    /** @brief External IP network */
    in_addr_t ext_net_;

    /** @brief External IP network mask */
    in_addr_t ext_netmask_;

    /** @brief External IP broadcast address */
    in_addr_t ext_broadcast_;
};

#endif /* NETFILTER_HH_ */
