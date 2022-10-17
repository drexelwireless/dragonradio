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
#include "net/TunTap.hh"
#include "util/capabilities.hh"
#include "util/net.hh"
#include "util/ssprintf.hh"
#include "util/threads.hh"

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
         std::bind(&TunTap::send, this, std::placeholders::_1))
  , source(*this,
           std::bind(&TunTap::start, this),
           std::bind(&TunTap::stop, this))
  , logger_(logger)
  , persistent_(persistent)
  , tap_iface_(tap_iface)
  , tap_ipaddr_(tap_ipaddr)
  , tap_ipnetmask_(tap_ipnetmask)
  , tap_macaddr_(tap_macaddr)
  , mtu_(mtu)
  , fd_(0)
  , ifr_({{0}})
{
    done_.store(true, std::memory_order_release);

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
    Socket sockfd(AF_INET, SOCK_DGRAM, 0);

    // Set MTU to 1500
    ifr_.ifr_mtu = mtu;

    if (ioctl(sockfd, SIOCSIFMTU, &ifr_) < 0)
        logTunTap(LOGERROR, "Error configuring mtu: %s", strerror(errno));

    // Set MAC address
    ifr_.ifr_hwaddr = {0};

    parseMAC(nodeMACAddress(node_id), &ifr_.ifr_hwaddr);

    if (ioctl(sockfd, SIOCSIFHWADDR, &ifr_) < 0)
        logTunTap(LOGERROR, "Error setting MAC address: %s", strerror(errno));

    // Set IP address
    ifr_.ifr_addr = {0};

    parseIP(nodeIPAddress(node_id), &ifr_.ifr_addr);

    if (ioctl(sockfd, SIOCSIFADDR, &ifr_) < 0)
        logTunTap(LOGERROR, "Error setting IP address: %s", strerror(errno));

    // Set netmask
    ifr_.ifr_addr = {0};

    parseIP(tap_ipnetmask_, &ifr_.ifr_addr);

    if (ioctl(sockfd, SIOCSIFNETMASK, &ifr_) < 0)
        logTunTap(LOGERROR, "Error setting IP netmask: %s", strerror(errno));

    // Bring up interface
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr_) < 0)
        logTunTap(LOGERROR, "Error bringing up interface: %s", strerror(errno));

    ifr_.ifr_flags |= IFF_UP | IFF_RUNNING;

    if (ioctl(sockfd, SIOCSIFFLAGS, &ifr_) < 0)
        logTunTap(LOGERROR, "Error bringing up interface: %s", strerror(errno));
}

TunTap::~TunTap(void)
{
    stop();
    closeTap();
}

static const char *clonedev = "/dev/net/tun";

void TunTap::openTap(std::string& dev, int flags)
{
    RaiseCaps    caps({CAP_NET_ADMIN});
    struct ifreq ifr = {{0}};

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

std::string TunTap::nodeMACAddress(uint8_t node_id) const
{
    return ssprintf(tap_macaddr_.c_str(), node_id);
}

std::string TunTap::nodeIPAddress(uint8_t node_id) const
{
    return ssprintf(tap_ipaddr_.c_str(), node_id);
}

std::string TunTap::sysConfPath(bool ipv4, const std::string &attr) const
{
    return ssprintf("/proc/sys/net/%s/conf/%s/%s",
                    ipv4 ? "ipv4" : "ipv6",
                    tap_iface_.c_str(),
                    attr.c_str());
}

std::string TunTap::readSysConfPath(bool ipv4, const std::string &attr) const
{
    std::string path = sysConfPath(ipv4, attr);

    Fd fd;

    if ((fd = open(path.c_str(), O_RDONLY)) < 0)
        throw std::runtime_error(strerror(errno));

    char    buf[1024];
    ssize_t count;

    if ((count = read(fd, buf, sizeof(buf)-1)) < 0)
        throw std::runtime_error(strerror(errno));

    buf[count] = '\0';
    return std::string(buf);
}

void TunTap::writeSysConfPath(bool ipv4, const std::string &attr, const std::string &value)
{
    RaiseCaps   caps({CAP_NET_ADMIN});
    std::string path = sysConfPath(ipv4, attr);

    Fd fd;

    if ((fd = open(path.c_str(), O_WRONLY)) < 0)
        throw std::runtime_error(strerror(errno));

    ssize_t count;

    if ((count = write(fd, value.c_str(), value.length()+1)) < 0)
        throw std::runtime_error(strerror(errno));
}

void TunTap::send(std::shared_ptr<RadioPacket>&& pkt)
{
    ssize_t nwrite;

    if ((nwrite = write(fd_, pkt->data() + sizeof(ExtendedHeader), pkt->ehdr().data_len)) < 0) {
        logTunTap(LOGERROR, "write error: errno=%s (%d); nwrite = %ld; size=%u; seq=%u; data_len=%u",
            strerror(errno),
            errno,
            nwrite,
            (unsigned) pkt->size(),
            (unsigned) pkt->hdr.seq,
            (unsigned) pkt->ehdr().data_len);
        return;
    }

    pkt->tuntap_timestamp = MonoClock::now();

    if (logger_ && logger_->getCollectSource(Logger::kRecvPackets))
        logger_->logRecv(pkt);

    if ((size_t) nwrite != pkt->ehdr().data_len) {
        logTunTap(LOGERROR, "incomplete write: nwrite = %ld; size=%u; seq=%u; data_len=%u",
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
    done_.store(false, std::memory_order_release);
    worker_thread_ = std::thread(&TunTap::worker, this);
}

void TunTap::stop(void)
{
    if (!done_.load(std::memory_order_acquire)) {
        done_.store(true, std::memory_order_release);

        wakeThread(worker_thread_);

        if (worker_thread_.joinable())
            worker_thread_.join();
    }
}

void TunTap::worker(void)
{
    makeThreadWakeable();

    while (!done_.load(std::memory_order_acquire)) {
        size_t  maxlen = mtu_ + sizeof(struct ether_header);
        auto    pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + maxlen);
        ssize_t nread;

        if ((nread = read(fd_, pkt->data() + sizeof(ExtendedHeader), maxlen)) < 0) {
            if (errno == EINTR) {
                logTunTap(LOGERROR, "read error: errno=%s (%d)",
                    strerror(errno),
                    errno);
                continue;
            }

            logTunTap(LOGERROR, "read error: errno=%s (%d)",
                strerror(errno),
                errno);
            exit(1);
        }

        pkt->hdr.flags.has_seq = 1;
        pkt->ehdr().data_len = nread;
        pkt->resize(sizeof(ExtendedHeader) + nread);
        pkt->timestamp = MonoClock::now();
        pkt->timestamps.tuntap_timestamp = WallClock::to_wall_time(pkt->timestamp);
        source.push(std::move(pkt));
    }
}
