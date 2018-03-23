/* TunTap.hh
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 * 
 */
#ifndef TUNTAP_HH_
#define TUNTAP_HH_

#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <list>
#include <iostream>
//The order of these 4 matter
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
//
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>

class TunTap
{
	public:
		int cwrite(char *buf, int n);
		int cread(char *buf, int n);
		int tap_alloc(char *dev, int flags);
		void close_interface();
        void add_arp_entries(unsigned int num_nodes_in_net, unsigned char* nodes_in_net);
		TunTap(std::string tap, unsigned int node_id, unsigned int num_nodes_in_net, unsigned char* nodes_in_net);
	private:
		int tap_fd;
		fd_set tx_set;
		unsigned int BUFSIZE;
		bool persistent_interface;
		char user[20];
		char cmd[80];
		char tap_name[IFNAMSIZ];
        unsigned char node_id;
};

#endif	// TUNTAP_HH_
