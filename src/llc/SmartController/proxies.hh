// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SMARTCONTROLLER_PROXIES_HH_
#define SMARTCONTROLLER_PROXIES_HH_

#include "llc/SmartController.hh"

/** @brief A proxy object for a SmartController send window */
class SendWindowProxy
{
public:
    SendWindowProxy(std::shared_ptr<SmartController> controller,
                    NodeId node_id)
      : controller_(controller)
      , node_id_(node_id)
    {
    }

    std::optional<double> getShortPER(void)
    {
        SendWindow                  &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        return sendw.short_per.value();
    }

    std::optional<double> getLongPER(void)
    {
        SendWindow                  &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        return sendw.long_per.value();
    }

    std::optional<double> getShortEVM(void)
    {
        SendWindow                  &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        return sendw.short_evm;
    }

    std::optional<double> getLongEVM(void)
    {
        SendWindow                  &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        return sendw.long_evm;
    }

    std::optional<double> getShortRSSI(void)
    {
        SendWindow                  &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        return sendw.short_rssi;
    }

    std::optional<double> getLongRSSI(void)
    {
        SendWindow                  &sendw = controller_->getSendWindow(node_id_);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        return sendw.long_rssi;
    }

private:
    /** @brief This send window's SmartController */
    std::shared_ptr<SmartController> controller_;

    /** @brief This send window's node ID */
    const NodeId node_id_;
};

/** @brief A proxy object for SmartController's send windows */
class SendWindowsProxy
{
public:
    SendWindowsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    SendWindowsProxy() = delete;
    ~SendWindowsProxy() = default;

    SendWindowProxy operator [](NodeId node)
    {
        return SendWindowProxy(controller_, node);
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

/** @brief A proxy object for a SmartController receive window */
class ReceiveWindowProxy
{
public:
    ReceiveWindowProxy(std::shared_ptr<SmartController> controller,
                       NodeId node_id)
      : controller_(controller)
      , node_id_(node_id)
    {
    }

    std::optional<double> getShortEVM(void)
    {
        RecvWindow                  &recvw = controller_->getReceiveWindow(node_id_);
        std::lock_guard<std::mutex> lock(recvw.mutex);

        return recvw.short_evm.value();
    }

    std::optional<double> getLongEVM(void)
    {
        RecvWindow                  &recvw = controller_->getReceiveWindow(node_id_);
        std::lock_guard<std::mutex> lock(recvw.mutex);

        return recvw.long_evm.value();
    }

    std::optional<double> getShortRSSI(void)
    {
        RecvWindow                  &recvw = controller_->getReceiveWindow(node_id_);
        std::lock_guard<std::mutex> lock(recvw.mutex);

        return recvw.short_rssi.value();
    }

    std::optional<double> getLongRSSI(void)
    {
        RecvWindow                  &recvw = controller_->getReceiveWindow(node_id_);
        std::lock_guard<std::mutex> lock(recvw.mutex);

        return recvw.long_rssi.value();
    }

private:
    /** @brief This send window's SmartController */
    std::shared_ptr<SmartController> controller_;

    /** @brief This send window's node ID */
    const NodeId node_id_;
};

/** @brief A proxy object for SmartController's receive windows */
class ReceiveWindowsProxy
{
public:
    ReceiveWindowsProxy(std::shared_ptr<SmartController> controller)
      : controller_(controller)
    {
    }

    ReceiveWindowsProxy() = delete;
    ~ReceiveWindowsProxy() = default;

    ReceiveWindowProxy operator [](NodeId node)
    {
        return ReceiveWindowProxy(controller_, node);
    }

private:
    /** @brief This object's SmartController */
    std::shared_ptr<SmartController> controller_;
};

#endif /* SMARTCONTROLLER_PROXIES_HH_ */
