/* TunTap.hh
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 *
 */
#ifndef TUNTAP_HH_
#define TUNTAP_HH_

#include <string>
#include <vector>

#include "Node.hh"

class TunTap
{
public:
    TunTap(const std::string& tap, NodeId node_id, const std::vector<unsigned char>& nodes_in_net);

    ssize_t cwrite(void *buf, size_t n);
    ssize_t cread(void *buf, size_t n);
    int tap_alloc(std::string& dev, int flags);
    void close_interface();
    void add_arp_entries(const std::vector<unsigned char>& nodes_in_net);

private:
    bool        persistent_interface;
    std::string tap;
    int         tap_fd;
    NodeId      node_id;
};

#endif    // TUNTAP_HH_
