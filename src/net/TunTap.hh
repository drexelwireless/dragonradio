/* TunTap.hh
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 *
 */
#ifndef TUNTAP_HH_
#define TUNTAP_HH_

#include <string>
#include <vector>

class TunTap
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
    ~TunTap();

    /** @brief Write to the tun/tap device */
    ssize_t cwrite(const void *buf, size_t n);

    /** @brief Read from the tun/tap device */
    ssize_t cread(void *buf, size_t n);

    /** @brief Add an ARP table entry with the given last octet */
    void add_arp_entry(uint8_t last_octet);

    /** @brief Delete an ARP table entry with the given last octet */
    void delete_arp_entry(uint8_t last_octet);

private:
    /** @brief Flag indicatign whether or not the interface is persistent. */
    bool _persistent;

    /** @brief The name of the tun/tap device */
    std::string _tapdev;

    /** @brief A sprintf-style format string for tun/tap IP address */
    const std::string _ip_fmt;

    /** @brief A sprintf-style format string for tun/tap MAC addresses */
    const std::string _mac_fmt;

    /** @brief File descriptor for tun/tap device */
    int _fd;

    /** @brief Create and open a tun/tap device.
     * @param dev The name of the device to open; may be the empty string. This
     * string will be assigned the actual device's name once it is created.
     * @param flags Flags to pass in the ifr_flags field of the ifreq given to
     * ioctl.
     */
    void open_tap(std::string& dev, int flags);

    /** @brief Close the tun/tap device. */
    void close_tap(void);
};

#endif    // TUNTAP_HH_
