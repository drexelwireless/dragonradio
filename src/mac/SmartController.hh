#ifndef SMARTCONTROLLER_H_
#define SMARTCONTROLLER_H_

#include "mac/Controller.hh"

/** @brief A MAC controller that implements ARQ. */
class SmartController : public Controller
{
public:
    SmartController(std::shared_ptr<Net> net);
    virtual ~SmartController() = default;

    bool pull(std::shared_ptr<NetPacket>& pkt) override;

    void received(std::shared_ptr<RadioPacket>&& pkt) override;
};

#endif /* SMARTCONTROLLER_H_ */
