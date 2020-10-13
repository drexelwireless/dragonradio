// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef DUMMYCONTROLLER_H_
#define DUMMYCONTROLLER_H_

#include <unordered_map>

#include "spinlock_mutex.hh"
#include "mac/Controller.hh"

/** @brief A Dummy MAC controller that just passes packets. */
class DummyController : public Controller
{
public:
    DummyController(std::shared_ptr<Net> net,
                    size_t mtu)
      : Controller(net, mtu)
    {
    }

    virtual ~DummyController() = default;

    bool pull(std::shared_ptr<NetPacket> &pkt) override;

    void received(std::shared_ptr<RadioPacket> &&pkt) override;

private:
    /** @brief Mutex for sequence numbers */
    spinlock_mutex seqs_mutex_;

    /** @brief Receive windows */
    std::unordered_map<NodeId, Seq> seqs_;
};

#endif /* DUMMYCONTROLLER_H_ */
