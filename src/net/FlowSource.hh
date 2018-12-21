#ifndef FLOWSOURCE_HH_
#define FLOWSOURCE_HH_

#include "net/FlowInfo.hh"

/** @brief A packet processor that collects information about flows sources. */
class FlowSource : public FlowProcessor<std::shared_ptr<NetPacket>>
{
public:
    explicit FlowSource(double mp)
      : FlowProcessor<std::shared_ptr<NetPacket>>(mp)
    {
    }

    FlowSource() = delete;

    virtual ~FlowSource() = default;

protected:
    bool process(std::shared_ptr<NetPacket> &pkt) override;
};

#endif /* FLOWSOURCE_HH_ */
