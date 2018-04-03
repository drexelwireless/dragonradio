/* TunTap.hh
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 *
 */
#ifndef TUNTAP_HH_
#define TUNTAP_HH_

#include <string>

class TunTap
{
public:
    TunTap(const std::string& tap, unsigned int node_id, unsigned int num_nodes_in_net, unsigned char* nodes_in_net);

    int cwrite(char *buf, int n);
    int cread(char *buf, int n);
    int tap_alloc(std::string& dev, int flags);
    void close_interface();
    void add_arp_entries(unsigned int num_nodes_in_net, unsigned char* nodes_in_net);

private:
    bool          persistent_interface;
    std::string   tap;
    int           tap_fd;
    unsigned char node_id;
};

#endif    // TUNTAP_HH_
