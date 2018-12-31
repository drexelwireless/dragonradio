#ifndef FLOWSINK_HH_
#define FLOWSINK_HH_

#include "net/FlowInfo.hh"

/** @brief A packet processor that collects information about flow sinks. */
class FlowSink : public FlowProcessor<std::shared_ptr<RadioPacket>>
{
public:
    explicit FlowSink(double mp)
      : FlowProcessor<std::shared_ptr<RadioPacket>>(mp)
    {
    }

    FlowSink() = delete;

    virtual ~FlowSink() = default;

protected:
    bool process(std::shared_ptr<RadioPacket> &pkt) override;
};

#endif /* FLOWSINK_HH_ */
