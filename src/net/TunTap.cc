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
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>

#include <functional>

#include "logging.hh"
#include "Util.hh"
#include "net/Net.hh"
#include "net/TunTap.hh"

using namespace std::placeholders;

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
  , done_(true)
{
    logTunTap(LOGINFO, "Creating tap interface %s", tap_iface.c_str());

    if (!persistent_) {
        // Check if tap is already up
        if (exec({"ifconfig", tap_iface}) != 0) {
            //Get active user
            passwd *user_name = getpwuid(getuid());

            if (exec({"ip", "tuntap", "add", "dev", tap_iface_, "mode", "tap", "user", user_name->pw_name}) < 0)
                logTunTap(LOGERROR, "Could not add user to tap device");
        }

        // Set MTU size to 1500
        if (exec({"ifconfig", tap_iface_, "mtu", sprintf("%u", (unsigned) mtu)}) < 0)
            logTunTap(LOGERROR, "Error configuring mtu");

        // Assign mac address
        if (exec({"ifconfig", tap_iface_, "hw", "ether", nodeMACAddress(node_id)}) < 0)
            logTunTap(LOGERROR, "Error configuring MAC address");

        // Assign IP address
        if (exec({"ifconfig", tap_iface_, nodeIPAddress(node_id), "netmask", tap_ipnetmask_}) < 0)
            logTunTap(LOGERROR, "Error configuring IP address");

        // Bring up the interface in case it's not up yet
        if (exec({"ifconfig", tap_iface_, "up"}) < 0)
            logTunTap(LOGERROR, "Error bringing ip interface");
    }

    openTap(tap_iface_, IFF_TAP | IFF_NO_PI);
}

TunTap::~TunTap(void)
{
    stop();
    closeTap();
}

size_t TunTap::getMTU(void)
{
    return mtu_;
}

void TunTap::addARPEntry(uint8_t node_id)
{
    if (exec({"arp", "-i", tap_iface_, "-s", nodeIPAddress(node_id), nodeMACAddress(node_id)}) < 0)
        logTunTap(LOGERROR, "Error adding ARP entry for last octet %d.", node_id);
}

void TunTap::deleteARPEntry(uint8_t node_id)
{
    if (exec({"arp", "-d", nodeIPAddress(node_id)}) < 0)
        logTunTap(LOGERROR, "Error deleting ARP entry for last octet %d.", node_id);
}

const char *clonedev = "/dev/net/tun";

void TunTap::openTap(std::string& dev, int flags)
{
    struct ifreq ifr;
    int err;

    /* open the clone device */
    if((fd_ = open(clonedev, O_RDWR)) < 0) {
        logTunTap(LOGERROR, "Error connecting to tap interface %s", tap_iface_.c_str());
        exit(1);
    }

    /* prepare the struct ifr, of type struct ifreq */
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    strncpy(ifr.ifr_name, dev.c_str(), IFNAMSIZ-1);

    if ((err = ioctl(fd_, TUNSETIFF, (void *) &ifr)) < 0 ) {
        perror("ioctl()");
        close(fd_);
        logTunTap(LOGERROR, "Error connecting to tap interface %s", tap_iface_.c_str());
        exit(1);
    }

    /* if ioctl succeded, write back the name to the variable "dev"
     * so the caller can know it.
     */
    dev = ifr.ifr_name;
}

void TunTap::closeTap(void)
{
    logTunTap(LOGINFO, "Closing tap interface");

    // Detach Tap Interface
    close(fd_);

    if (!persistent_) {
        if (exec({"ip", "tuntap", "del", "dev", tap_iface_, "mode", "tap"}) < 0)
            logTunTap(LOGERROR, "Error deleting tap.");
    }
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
