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
    ~NetFilter();

protected:
    bool process(std::shared_ptr<NetPacket>& pkt) override;

private:
    /* @brief The Net we use to filter packets */
    std::shared_ptr<Net> net_;
};

#endif /* NETFILTER_HH_ */
