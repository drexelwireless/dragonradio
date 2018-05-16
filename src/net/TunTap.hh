/* TunTap.hh
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 *
 */
#ifndef TUNTAP_HH_
#define TUNTAP_HH_

#include <string>
#include <thread>
#include <vector>

#include "net/Element.hh"

class TunTap : public Element
{
public:
    /** @brief Create a tun/tap device.
     * @param tapdev The name of the tun/tap device to create.
     * @param persistent Is this device persistent, or should we create it now and
     * destroy it when we are destructed?
     * @param mtu MTU size for interface.
     * @param ip_fmt sprintf-style format string for tun/tap IP address
     * @param mac_fmt sprintf-style format string for tun/tap MAC address
     * @param last_octet Last octet of IP and MAC addresses
     */
    TunTap(const std::string& tapdev,
           bool persistent,
           size_t mtu,
           const std::string ip_fmt,
           const std::string mac_fmt,
           uint8_t last_octet);
    virtual ~TunTap();

    /** @brief Return the MTU of this interface */
    size_t getMTU(void);

    /** @brief Add an ARP table entry with the given last octet */
    void addARPEntry(uint8_t last_octet);

    /** @brief Delete an ARP table entry with the given last octet */
    void deleteARPEntry(uint8_t last_octet);

    /** @brief Sink for radio packets. Packets written here are sent to the
     * tun/tap device.
     */
    RadioIn<Push> sink;

    /** @brief Source for network packets. Packets read here are received from
     * the tun/tap device.
     */
    NetOut<Push> source;

private:
    /** @brief Flag indicatign whether or not the interface is persistent. */
    bool persistent_;

    /** @brief The name of the tun/tap device */
    std::string tapdev_;

    /** @brief MTU of the interface */
    size_t mtu_;

    /** @brief A sprintf-style format string for tun/tap IP address */
    const std::string ip_fmt_;

    /** @brief A sprintf-style format string for tun/tap MAC addresses */
    const std::string mac_fmt_;

    /** @brief File descriptor for tun/tap device */
    int fd_;

    /** @brief Create and open a tun/tap device.
     * @param dev The name of the device to open; may be the empty string. This
     * string will be assigned the actual device's name once it is created.
     * @param flags Flags to pass in the ifr_flags field of the ifreq given to
     * ioctl.
     */
    void openTap(std::string& dev, int flags);

    /** @brief Close the tun/tap device. */
    void closeTap(void);

    /** @brief Write to the tun/tap device */
    ssize_t cwrite(const void *buf, size_t n);

    /** @brief Read from the tun/tap device */
    ssize_t cread(void *buf, size_t n);

    /** @brief Send a packet to the tun/tap device */
    void send(std::unique_ptr<RadioPacket>&& pkt);

    /** @brief Flag indicating whether or not we are done receiving */
    bool done_;

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
