#ifndef DUMMYCONTROLLER_H_
#define DUMMYCONTROLLER_H_

#include "mac/Controller.hh"

/** @brief A Dummy MAC controller that just passes packets. */
class DummyController : public Controller
{
public:
    DummyController(std::shared_ptr<Net> net);
    virtual ~DummyController() = default;

    bool pull(std::shared_ptr<NetPacket>& pkt) override;

    void received(std::shared_ptr<RadioPacket>&& pkt) override;

    void missed(std::shared_ptr<NetPacket>&& pkt) override;

    void transmitted(std::shared_ptr<NetPacket>& pkt) override;
};

#endif /* DUMMYCONTROLLER_H_ */
