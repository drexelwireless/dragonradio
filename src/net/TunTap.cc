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

#include "Node.hh"
#include "Util.hh"
#include "net/TunTap.hh"

#define IP_FMT  "10.10.10.%d"
#define MAC_FMT "c6:ff:ff:ff:%02x"

TunTap::TunTap(const std::string& tap, NodeId node_id, const std::vector<unsigned char>& nodes_in_net)
    : persistent_interface(true), tap(tap), node_id(node_id)
{
    persistent_interface = false;

    if (!persistent_interface)
    {
        // Check if tap is already up
        if (sys("ifconfig %s > /dev/null 2>&1", tap.c_str()) != 0) {
            //Get active user
            passwd *user_name = getpwuid(getuid());

            if (sys("ip tuntap add dev %s mode tap user %s", tap.c_str(), user_name->pw_name) < 0)
                fprintf(stderr, "Could not add user to tap device\n");
        }

        // Set MTU size to 1500
        if (sys("ifconfig %s mtu 1500", tap.c_str()) < 0)
            fprintf(stderr, "system() - ifconfig mtu\n");

        // Assign mac address
        if (sys("ifconfig %s hw ether " MAC_FMT, tap.c_str(), node_id) < 0)
            fprintf(stderr, "Error configuring mac address.\n");

        // Assign IP address
        if (sys("ifconfig %s " IP_FMT, tap.c_str(), node_id) < 0)
            fprintf(stderr, "system() - ifconfig\n");

        // Bring up the interface in case it's not up yet
        if (sys("ifconfig %s up", tap.c_str()) < 0)
            fprintf(stderr, "system() - error bringing interface up\n");
    }

    tap_fd = tap_alloc(this->tap, IFF_TAP | IFF_NO_PI);
    if (tap_fd < 0) {
        fprintf(stderr, "Error connecting to tap interface %s\n", tap.c_str());
        exit(1);
    }

    add_arp_entries(nodes_in_net);
}

ssize_t TunTap::cwrite(void *buf, size_t n)
{
    ssize_t nwrite;

    if ((nwrite = write(tap_fd, buf, n)) < 0) {
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
    FD_SET(tap_fd, &tx_set);

    if (select(tap_fd + 1, &tx_set, NULL, NULL, &timeout) < 0) {
        perror("select()");
        exit(1);
    }

    ssize_t nread = 0;

    if (FD_ISSET(tap_fd, &tx_set)) {
        if ((nread = read(tap_fd, buf, n)) < 0) {
            perror("read()");
            exit(1);
        }
    }

    return nread;
}

const char *clonedev = "/dev/net/tun";

int TunTap::tap_alloc(std::string& dev, int flags)
{
    /* Arguements
     *    char *dev
     *    name of an interface (or '\0')
     *    int flags
     *    interface flags (eg, IFF_TUN | IFF_NO_PI)
     * */

    struct ifreq ifr;
    int fd, err;

    /* open the clone device */
    if((fd = open(clonedev, O_RDWR)) < 0)
        return fd;

    /* prepare the struct ifr, of type struct ifreq */
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    strncpy(ifr.ifr_name, dev.c_str(), IFNAMSIZ);

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
        perror("ioctl()");
        close(fd);
        return err;
    }

    /* if ioctl succeded, write back the name to the variable "dev"
     * so the caller can know it.
     */
    dev = ifr.ifr_name;

    /* special file descriptor the caller will use to talk
     * with the virtual interface
     */
    return fd;

}

void TunTap::add_arp_entries(const std::vector<unsigned char>& nodes_in_net)
{
    unsigned char current_node;

    for(auto it = nodes_in_net.begin(); it != nodes_in_net.end(); it++)
    {
        current_node = *it;
        if (current_node != node_id) {
            if (sys("arp -i %s -s " IP_FMT " " MAC_FMT, tap.c_str(), current_node, current_node) < 0)
                fprintf(stderr, "Error setting arp table for node %d.\n", current_node);
        }
    }
}

void TunTap::close_interface()
{
    // Detach Tap Interface
    close(tap_fd);

    if (!persistent_interface) {
        if (sys("ip tuntap devl dev %s mode tap", tap.c_str()) < 0)
            fprintf(stderr, "Error deleting tap.");
    }
}
