// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TUNTAP_HH_
#define TUNTAP_HH_

#include <sys/ioctl.h>
#include <net/if.h>

#include <string>
#include <thread>
#include <vector>

#include "Logger.hh"
#include "net/Element.hh"

class TunTap : public Element
{
public:
    /** @brief Create a tun/tap device.
     * @param tap_iface The name of the tun/tap device to create.
     * @param tap_ipaddr IP address for tap interface.
     * @param tap_ipnetmask Netmask for tap interface
     * @param tap_macaddr MAC address for tap interface.
     * @param persistent Is this device persistent, or should we create it now and
     * destroy it when we are destructed?
     * @param mtu MTU size for interface.
     * @param node_id Node ID
     */
    TunTap(const std::string& tap_iface,
           const std::string& tap_ipaddr,
           const std::string& tap_ipnetmask,
           const std::string& tap_macaddr,
           bool persistent,
           size_t mtu,
           uint8_t node_id);
    virtual ~TunTap();

    /** @brief Return the MTU of this interface */
    size_t getMTU(void);

    /** @brief Add a static ARP table entry to tap device for node */
    void addARPEntry(uint8_t node_id);

    /** @brief Delete ARP table entryfrom tap device for node */
    void deleteARPEntry(uint8_t node_id);

    /** @brief Sink for radio packets. Packets written here are sent to the
     * tun/tap device.
     */
    RadioIn<Push> sink;

    /** @brief Source for network packets. Packets read here are received from
     * the tun/tap device.
     */
    NetOut<Push> source;

private:
    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Flag indicating whether or not the interface is persistent. */
    bool persistent_;

    /** @brief The name of the tun/tap device */
    std::string tap_iface_;

    /** @brief The name of the tun/tap device */
    std::string tap_ipaddr_;

    /** @brief The name of the tun/tap device */
    std::string tap_ipnetmask_;

    /** @brief The name of the tun/tap device */
    std::string tap_macaddr_;

    /** @brief MTU of the interface */
    size_t mtu_;

    /** @brief File descriptor for tun/tap device */
    int fd_;

    /** @brief ifreq for configuring tap interface  */
    struct ifreq ifr_;

    /** @brief Flag indicating whether or not we are done receiving */
    bool done_;

    /** @brief Create and open a tun/tap device.
     * @param dev The name of the device to open; may be the empty string. This
     * string will be assigned the actual device's name once it is created.
     * @param flags Flags to pass in the ifr_flags field of the ifreq given to
     * ioctl.
     */
    void openTap(std::string& dev, int flags);

    /** @brief Close the tun/tap device. */
    void closeTap(void);

    /** @brief Get MAC address for node. */
    std::string nodeMACAddress(uint8_t node_id);

    /** @brief Get IP address for node. */
    std::string nodeIPAddress(uint8_t node_id);

    /** @brief Send a packet to the tun/tap device */
    void send(std::shared_ptr<RadioPacket>&& pkt);

    /** @brief Start the receive worker */
    void start(void);

    /** @brief Stop the receive worker */
    void stop(void);

    /** @brief Receive worker thread */
    std::thread worker_thread_;

    /** @brief Receive worker */
    void worker(void);
};

#endif    // TUNTAP_HH_
