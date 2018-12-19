#ifndef NETFILTER_HH_
#define NETFILTER_HH_

#include <functional>

#include "net/Net.hh"
#include "net/Processor.hh"

using namespace std::placeholders;

class NetFilter : public Processor<std::shared_ptr<NetPacket>>
{
public:
    NetFilter(std::shared_ptr<Net> net);
    virtual ~NetFilter() = default;

protected:
    bool process(std::shared_ptr<NetPacket>& pkt) override;

private:
    /* @brief The Net we use to filter packets */
    std::shared_ptr<Net> net_;

    /** @brief Internal IP network */
    uint32_t int_net_;

    /** @brief Internal IP network mask */
    uint32_t int_netmask_;

    /** @brief Internal IP broadcast address */
    uint32_t int_broadcast_;

    /** @brief External IP network */
    uint32_t ext_net_;

    /** @brief External IP network mask */
    uint32_t ext_netmask_;

    /** @brief External IP broadcast address */
    uint32_t ext_broadcast_;
};

#endif /* NETFILTER_HH_ */
