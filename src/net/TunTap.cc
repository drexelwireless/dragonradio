// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

/* TunTap.cc
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 *
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/if_ether.h>
#include <linux/if_tun.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <functional>

#include "logging.hh"
#include "RadioNet.hh"
#include "Util.hh"
#include "net/TunTap.hh"

using namespace std::placeholders;

static void parseMAC(const std::string &s, struct sockaddr &addr)
{
    addr.sa_family = ARPHRD_ETHER;

    if (sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               (unsigned char*) &addr.sa_data[0],
               (unsigned char*) &addr.sa_data[1],
               (unsigned char*) &addr.sa_data[2],
               (unsigned char*) &addr.sa_data[3],
               (unsigned char*) &addr.sa_data[4],
               (unsigned char*) &addr.sa_data[5]) != 6)
        throw std::domain_error("Illegally formatted MAC address");
}

static void parseIP(const std::string &s, struct sockaddr &addr)
{
    addr.sa_family = AF_INET;

    if (inet_pton(AF_INET, s.c_str(), &((struct sockaddr_in*) &addr)->sin_addr) == 0)
        throw std::domain_error("Illegally formatted IP address");
}

/** @file TunTap.cc
 * This code has been heavily modified from the version included in the
 * reference SC2 radio. That code appears to have been taken from this
 * [Tun/Tap interface tutorial](http://backreference.org/2010/03/26/tuntap-interface-tutorial/)
 * without attribution.
 */

TunTap::TunTap(const std::string& tap_iface,
               const std::string& tap_ipaddr,
               const std::string& tap_ipnetmask,
               const std::string& tap_macaddr,
               bool persistent,
               size_t mtu,
               uint8_t node_id)
  : sink(*this,
         nullptr,
         nullptr,
         std::bind(&TunTap::send, this, _1))
  , source(*this,
           std::bind(&TunTap::start, this),
           std::bind(&TunTap::stop, this))
  , persistent_(persistent)
  , tap_iface_(tap_iface)
  , tap_ipaddr_(tap_ipaddr)
  , tap_ipnetmask_(tap_ipnetmask)
  , tap_macaddr_(tap_macaddr)
  , mtu_(mtu)
  , fd_(0)
  , sockfd_(0)
  , ifr_({0})
  , done_(true)
{
    logTunTap(LOGINFO, "Creating tap interface %s", tap_iface.c_str());

    openTap(tap_iface_, IFF_TAP | IFF_NO_PI);

    // Set network interface options
    RaiseCaps caps({CAP_NET_ADMIN});

    // Copy interface name to ifr_ for later use
    strncpy(ifr_.ifr_name, tap_iface_.c_str(), IFNAMSIZ-1);

    // Set tap device ownership
    if (ioctl(fd_, TUNSETOWNER, getuid()) < 0)
        logTunTap(LOGERROR, "Could not set owner: %s", strerror(errno));

    if (ioctl(fd_, TUNSETGROUP, getgid()) < 0)
        logTunTap(LOGERROR, "Could not set group: %s", strerror(errno));

    // Make tap device persistent
    if (persistent) {
        if (ioctl(fd_, TUNSETPERSIST, 1) < 0)
            logTunTap(LOGERROR, "Could not make tap device persistent: %s", strerror(errno));
    }

    // Create socket for use with ioctl
    if ((sockfd_ = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        logTunTap(LOGERROR, "Could not create socket: %s", strerror(errno));

    // Set MTU to 1500
    ifr_.ifr_mtu = mtu;

    if (ioctl(sockfd_, SIOCSIFMTU, &ifr_) < 0)
        logTunTap(LOGERROR, "Error configuring mtu: %s", strerror(errno));

    // Set MAC address
    ifr_.ifr_hwaddr = {0};

    parseMAC(nodeMACAddress(node_id), ifr_.ifr_hwaddr);

    if (ioctl(sockfd_, SIOCSIFHWADDR, &ifr_) < 0)
        logTunTap(LOGERROR, "Error setting MAC address: %s", strerror(errno));

    // Set IP address
    ifr_.ifr_addr = {0};

    parseIP(nodeIPAddress(node_id), ifr_.ifr_addr);

    if (ioctl(sockfd_, SIOCSIFADDR, &ifr_) < 0)
        logTunTap(LOGERROR, "Error setting IP address: %s", strerror(errno));

    // Set netmask
    ifr_.ifr_addr = {0};

    parseIP(tap_ipnetmask_, ifr_.ifr_addr);

    if (ioctl(sockfd_, SIOCSIFNETMASK, &ifr_) < 0)
        logTunTap(LOGERROR, "Error setting IP netmask: %s", strerror(errno));

    // Bring up interface
    if (ioctl(sockfd_, SIOCGIFFLAGS, &ifr_) < 0)
        logTunTap(LOGERROR, "Error bringing up interface: %s", strerror(errno));

    ifr_.ifr_flags |= IFF_UP | IFF_RUNNING;

    if (ioctl(sockfd_, SIOCSIFFLAGS, &ifr_) < 0)
        logTunTap(LOGERROR, "Error bringing up interface: %s", strerror(errno));
}

TunTap::~TunTap(void)
{
    stop();
    closeTap();
    close(sockfd_);
}

size_t TunTap::getMTU(void)
{
    return mtu_;
}

void TunTap::addARPEntry(uint8_t node_id)
{
    RaiseCaps     caps({CAP_NET_ADMIN});
    struct arpreq req = {0};

    strncpy(req.arp_dev, tap_iface_.c_str(), sizeof(req.arp_dev)-1);

    parseIP(nodeIPAddress(node_id), req.arp_pa);
    parseMAC(nodeMACAddress(node_id), req.arp_ha);

    req.arp_flags = ATF_PERM | ATF_COM;

    if (ioctl(sockfd_, SIOCSARP, &req) < 0)
        logTunTap(LOGERROR, "Error adding ARP entry for node %d: %s", node_id, strerror(errno));
    else
        logTunTap(LOGDEBUG, "Added ARP entry for node %d", node_id);
}

void TunTap::deleteARPEntry(uint8_t node_id)
{
    RaiseCaps     caps({CAP_NET_ADMIN});
    struct arpreq req = {0};

    strncpy(req.arp_dev, tap_iface_.c_str(), sizeof(req.arp_dev)-1);

    parseIP(nodeIPAddress(node_id), req.arp_pa);

    if (ioctl(sockfd_, SIOCDARP, &req) < 0)
        logTunTap(LOGERROR, "Error deleting ARP entry for node %d: %s", node_id, strerror(errno));
    else
        logTunTap(LOGDEBUG, "Deleted ARP entry for node %d", node_id);
}

static const char *clonedev = "/dev/net/tun";

void TunTap::openTap(std::string& dev, int flags)
{
    RaiseCaps    caps({CAP_NET_ADMIN});
    struct ifreq ifr = {0};

    // Open the clone device
    if((fd_ = open(clonedev, O_RDWR)) < 0) {
        logTunTap(LOGERROR, "Error connecting to tap interface %s: %s", tap_iface_.c_str(), strerror(errno));
        exit(1);
    }

    // Create the tap interface
    ifr.ifr_flags = flags;

    strncpy(ifr.ifr_name, dev.c_str(), IFNAMSIZ-1);

    if ((ioctl(fd_, TUNSETIFF, &ifr)) < 0 ) {
        close(fd_);
        logTunTap(LOGERROR, "Error connecting to tap interface %s: %s", tap_iface_.c_str(), strerror(errno));
        exit(1);
    }

    // If ioctl succeeded, write the interface name back to the variable dev so
    // the caller knows what it is.
    dev = ifr.ifr_name;
}

void TunTap::closeTap(void)
{
    RaiseCaps caps({CAP_NET_ADMIN});

    logTunTap(LOGINFO, "Closing tap interface");

    // If the interface isn't persistent, bring it down
    if (!persistent_) {
        if (ioctl(fd_, TUNSETPERSIST, 0) < 0)
            logTunTap(LOGERROR, "Error deleting tap: %s", strerror(errno));
    }

    // Close Tap Interface
    close(fd_);
}

std::string TunTap::nodeMACAddress(uint8_t node_id)
{
    return sprintf(tap_macaddr_.c_str(), node_id);
}

std::string TunTap::nodeIPAddress(uint8_t node_id)
{
    return sprintf(tap_ipaddr_.c_str(), node_id);
}

void TunTap::send(std::shared_ptr<RadioPacket>&& pkt)
{
    ssize_t nwrite;

    if ((nwrite = write(fd_, pkt->data() + sizeof(ExtendedHeader), pkt->ehdr().data_len)) < 0) {
        logTunTap(LOGERROR, "tun/tap write failure: errno=%s (%d); nwrite = %ld; size=%u; seq=%u; data_len=%u\n",
            strerror(errno),
            errno,
            nwrite,
            (unsigned) pkt->size(),
            (unsigned) pkt->hdr.seq,
            (unsigned) pkt->ehdr().data_len);
        return;
    }

    if ((size_t) nwrite != pkt->ehdr().data_len) {
        logTunTap(LOGERROR, "tun/tap incomplete write: nwrite = %ld; size=%u; seq=%u; data_len=%u\n",
            nwrite,
            (unsigned) pkt->size(),
            (unsigned) pkt->hdr.seq,
            (unsigned) pkt->ehdr().data_len);
        return;
    }

    logTunTap(LOGDEBUG-1, "Wrote %lu bytes (seq# %u) from %u to %u (evm = %.2f; rssi = %.2f)",
        (unsigned long) nwrite,
        (unsigned int) pkt->hdr.seq,
        (unsigned int) pkt->ehdr().src,
        (unsigned int) pkt->ehdr().dest,
        pkt->evm,
        pkt->rssi);
}

void TunTap::start(void)
{
    done_ = false;
    worker_thread_ = std::thread(&TunTap::worker, this);
}

void TunTap::stop(void)
{
    done_ = true;

    wakeThread(worker_thread_);

    if (worker_thread_.joinable())
        worker_thread_.join();
}

void TunTap::worker(void)
{
    makeThreadWakeable();

    while (!done_) {
        size_t  maxlen = mtu_ + sizeof(struct ether_header);
        auto    pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + maxlen);
        ssize_t nread;

        if ((nread = read(fd_, pkt->data() + sizeof(ExtendedHeader), maxlen)) < 0) {
            if (errno == EINTR)
                continue;

            perror("read()");
            exit(1);
        }

        pkt->hdr.flags.has_data = 1;
        pkt->ehdr().data_len = nread;
        pkt->resize(sizeof(ExtendedHeader) + nread);
        pkt->timestamp = MonoClock::now();
        source.push(std::move(pkt));
    }
}
