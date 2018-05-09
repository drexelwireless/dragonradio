/* TunTap.cc
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>

#include "Util.hh"
#include "net/TunTap.hh"

/** @file TunTap.cc
 * This code has been heavily modified from the version included in the
 * reference SC2 radio. That code appears to have been taken from this
 * [Tun/Tap interface tutorial](http://backreference.org/2010/03/26/tuntap-interface-tutorial/)
 * without attribution.
 */

TunTap::TunTap(const std::string& tapdev,
               bool persistent,
               size_t mtu,
               const std::string ip_fmt,
               const std::string mac_fmt,
               uint8_t last_octet) :
    _persistent(persistent),
    _tapdev(tapdev),
    _ip_fmt(ip_fmt),
    _mac_fmt(mac_fmt)
{
    if (!_persistent) {
        // Check if tap is already up
        if (sys("ifconfig %s > /dev/null 2>&1", _tapdev.c_str()) != 0) {
            //Get active user
            passwd *user_name = getpwuid(getuid());

            if (sys("ip tuntap add dev %s mode tap user %s", _tapdev.c_str(), user_name->pw_name) < 0)
                fprintf(stderr, "Could not add user to tap device\n");
        }

        // Set MTU size to 1500
        if (sys("ifconfig %s mtu %u", _tapdev.c_str(), (unsigned) mtu) < 0)
            fprintf(stderr, "system() - ifconfig mtu\n");

        // Assign mac address
        if (sys(("ifconfig %s hw ether " + mac_fmt).c_str(), _tapdev.c_str(), last_octet) < 0)
            fprintf(stderr, "Error configuring mac address.\n");

        // Assign IP address
        if (sys(("ifconfig %s " + ip_fmt).c_str(), _tapdev.c_str(), last_octet) < 0)
            fprintf(stderr, "system() - ifconfig\n");

        // Bring up the interface in case it's not up yet
        if (sys("ifconfig %s up", _tapdev.c_str()) < 0)
            fprintf(stderr, "system() - error bringing interface up\n");
    }

    open_tap(_tapdev, IFF_TAP | IFF_NO_PI);
}

TunTap::~TunTap(void)
{
    close_tap();
}

ssize_t TunTap::cwrite(const void *buf, size_t n)
{
    ssize_t nwrite;

    if ((nwrite = write(_fd, buf, n)) < 0) {
        perror("Writing data");
        exit(1);
    }

    return nwrite;
}

ssize_t TunTap::cread(void *buf, size_t n)
{
    fd_set tx_set;
    struct timeval timeout = {1,0}; //1 second timeoout

    FD_ZERO(&tx_set);
    FD_SET(_fd, &tx_set);

    if (select(_fd + 1, &tx_set, NULL, NULL, &timeout) < 0) {
        perror("select()");
        exit(1);
    }

    ssize_t nread = 0;

    if (FD_ISSET(_fd, &tx_set)) {
        if ((nread = read(_fd, buf, n)) < 0) {
            perror("read()");
            exit(1);
        }
    }

    return nread;
}

void TunTap::add_arp_entry(uint8_t last_octet)
{
    if (sys(("arp -i %s -s " + _ip_fmt + " " + _mac_fmt).c_str(), _tapdev.c_str(), last_octet, last_octet) < 0)
        fprintf(stderr, "Error adding ARP entry for last octet %d.\n", last_octet);
}

void TunTap::delete_arp_entry(uint8_t last_octet)
{
    if (sys(("arp -d " + _ip_fmt).c_str(), last_octet) < 0)
        fprintf(stderr, "Error deleting ARP entry for last octet %d.\n", last_octet);
}

const char *clonedev = "/dev/net/tun";

void TunTap::open_tap(std::string& dev, int flags)
{
    struct ifreq ifr;
    int err;

    /* open the clone device */
    if((_fd = open(clonedev, O_RDWR)) < 0) {
        fprintf(stderr, "Error connecting to tap interface %s\n", _tapdev.c_str());
        exit(1);
    }

    /* prepare the struct ifr, of type struct ifreq */
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    strncpy(ifr.ifr_name, dev.c_str(), IFNAMSIZ);

    if ((err = ioctl(_fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
        perror("ioctl()");
        close(_fd);
        fprintf(stderr, "Error connecting to tap interface %s\n", _tapdev.c_str());
        exit(1);
    }

    /* if ioctl succeded, write back the name to the variable "dev"
     * so the caller can know it.
     */
    dev = ifr.ifr_name;
}

void TunTap::close_tap(void)
{
    // Detach Tap Interface
    close(_fd);

    if (!_persistent) {
        if (sys("ip tuntap del dev %s mode tap", _tapdev.c_str()) < 0)
            fprintf(stderr, "Error deleting tap.");
    }
}
