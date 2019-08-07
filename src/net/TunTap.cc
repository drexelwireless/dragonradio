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

#include "Logger.hh"
#include "RadioConfig.hh"
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

TunTap::TunTap(const std::string& tapdev,
               bool persistent,
               size_t mtu,
               uint8_t node_id) :
    sink(*this,
         nullptr,
         nullptr,
         std::bind(&TunTap::send, this, _1)),
    source(*this,
           std::bind(&TunTap::start, this),
           std::bind(&TunTap::stop, this)),
    persistent_(persistent),
    tapdev_(tapdev),
    mtu_(mtu),
    done_(true)
{
    if (rc.verbose)
        printf("Creating tap interface %s\n", tapdev.c_str());

    if (!persistent_) {
        // Check if tap is already up
        if (sys("ifconfig %s > /dev/null 2>&1", tapdev_.c_str()) != 0) {
            //Get active user
            passwd *user_name = getpwuid(getuid());

            if (sys("ip tuntap add dev %s mode tap user %s", tapdev_.c_str(), user_name->pw_name) < 0)
                logEvent("TUNTAP: Could not add user to tap device");
        }

        // Set MTU size to 1500
        if (sys("ifconfig %s mtu %u", tapdev_.c_str(), (unsigned) mtu) < 0)
            logEvent("TUNTAP: system() - ifconfig mtu");

        // Assign mac address
        if (sys(("ifconfig %s hw ether " + kMACFmt).c_str(), tapdev_.c_str(), node_id) < 0)
            logEvent("TUNTAP: Error configuring mac address.");

        // Assign IP address
        if (sys(("ifconfig %s " + kIntIPFmt + " netmask 255.255.255.0").c_str(), tapdev_.c_str(), node_id) < 0)
            logEvent("TUNTAP: system() - ifconfig");

        // Bring up the interface in case it's not up yet
        if (sys("ifconfig %s up", tapdev_.c_str()) < 0)
            logEvent("TUNTAP: system() - error bringing interface up");
    }

    openTap(tapdev_, IFF_TAP | IFF_NO_PI);
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
    if (sys(("arp -i %s -s " + kIntIPFmt + " " + kMACFmt).c_str(), tapdev_.c_str(), node_id, node_id) < 0)
        logEvent("TUNTAP: Error adding ARP entry for last octet %d.", node_id);
}

void TunTap::deleteARPEntry(uint8_t node_id)
{
    if (sys(("arp -d " + kIntIPFmt).c_str(), node_id) < 0)
        logEvent("TUNTAP: Error deleting ARP entry for last octet %d.", node_id);
}

const char *clonedev = "/dev/net/tun";

void TunTap::openTap(std::string& dev, int flags)
{
    struct ifreq ifr;
    int err;

    /* open the clone device */
    if((fd_ = open(clonedev, O_RDWR)) < 0) {
        logEvent("TUNTAP: Error connecting to tap interface %s", tapdev_.c_str());
        exit(1);
    }

    /* prepare the struct ifr, of type struct ifreq */
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    strncpy(ifr.ifr_name, dev.c_str(), IFNAMSIZ);

    if ((err = ioctl(fd_, TUNSETIFF, (void *) &ifr)) < 0 ) {
        perror("ioctl()");
        close(fd_);
        logEvent("TUNTAP: Error connecting to tap interface %s", tapdev_.c_str());
        exit(1);
    }

    /* if ioctl succeded, write back the name to the variable "dev"
     * so the caller can know it.
     */
    dev = ifr.ifr_name;
}

void TunTap::closeTap(void)
{
    if (rc.verbose)
        printf("Closing tap interface\n");

    // Detach Tap Interface
    close(fd_);

    if (!persistent_) {
        if (sys("ip tuntap del dev %s mode tap", tapdev_.c_str()) < 0)
            logEvent("TUNTAP: Error deleting tap.");
    }
}

void TunTap::send(std::shared_ptr<RadioPacket>&& pkt)
{
    ssize_t nwrite;

    if ((nwrite = write(fd_, pkt->data() + sizeof(ExtendedHeader), pkt->hdr.data_len)) < 0) {
        logEvent("NET: tun/tap write failure: errno=%s (%d); nwrite = %ld; size=%u; seq=%u; data_len=%u\n",
            strerror(errno),
            errno,
            nwrite,
            (unsigned) pkt->size(),
            (unsigned) pkt->hdr.seq,
            (unsigned) pkt->hdr.data_len);
        return;
    }

    if ((size_t) nwrite != pkt->hdr.data_len) {
        logEvent("NET: tun/tap incomplete write: nwrite = %ld; size=%u; seq=%u; data_len=%u\n",
            nwrite,
            (unsigned) pkt->size(),
            (unsigned) pkt->hdr.seq,
            (unsigned) pkt->hdr.data_len);
        return;
    }

    if (rc.verbose_packet_trace)
        printf("Wrote %lu bytes (seq# %u) from %u to %u (evm = %.2f; rssi = %.2f)\n",
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

        pkt->hdr.data_len = nread;
        pkt->resize(sizeof(ExtendedHeader) + nread);
        pkt->timestamp = MonoClock::now();
        source.push(std::move(pkt));
    }
}
