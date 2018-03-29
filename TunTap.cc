/* TunTap.cc
 *
 * Distribution Statement “A” (Approved for Public Release, Distribution Unlimited)
 * 
 */
#include <TunTap.hh>

int TunTap::cwrite(char *buf, int n)
{
	int nwrite;
	if ((nwrite = write(tap_fd, buf, n)) < 0) {
		perror("Writing data");
		exit(1);
	}
	return nwrite;
}

int TunTap::cread(char *buf, int n)
{
	FD_ZERO(&tx_set);

	FD_SET(tap_fd, &tx_set);
	struct timeval timeout = {1,0}; //1 second timeoout
	int retval = select(tap_fd + 1, &tx_set, NULL, NULL, &timeout);
	if(retval < 0)
	{
		perror("select()");
		exit(1);
	}
	
	uint16_t nread = 0;
	if(FD_ISSET(tap_fd, &tx_set))
	{
		if ((nread = read(tap_fd, buf, n)) < 0) 
		{
			perror("read()");
			exit(1);
		}

	}
	return nread;
}

int TunTap::tap_alloc(char *dev, int flags)
{
	/* Arguements
	 *	char *dev
	 *	name of an interface (or '\0')
	 *	int flags
	 *	interface flags (eg, IFF_TUN | IFF_NO_PI)
	 * */

	struct ifreq ifr;
	int fd, err;
	const char *clonedev = "/dev/net/tun";

	/* open the clone device */
	if( (fd = open(clonedev, O_RDWR)) < 0 ) {
		return fd;
	}

	/* prepare the struct ifr, of type struct ifreq */
	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = flags;

	if (*dev) {
		/* if it is defined, put it in the structure
		 * otherwise the kernel will allocate the "next"
		 * device of the specified type
		 */
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if ( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
		perror("ioctl()");
		close(fd);
		return err;
	}

	/* if ioctl succeded, write back the name to the variable "dev"
	 * so the caller can know it.
	 */
	strcpy(dev, ifr.ifr_name);

	/* special file descriptor the caller will use to talk
	 * with the virtual interface
	 */
	return fd;

}

TunTap::TunTap(std::string tap, unsigned int node_id, unsigned int num_nodes_in_net, unsigned char* nodes_in_net)
    :persistent_interface(true), node_id(node_id)
{
	BUFSIZE = 1500;
    std::string tap_ip_address = "10.10.10." + std::to_string(node_id);
    std::string tap_mac_address;
    if(node_id > 99)
    {
        int first = node_id/100;
        int last = node_id - 100;
        if(last > 9)
        {
            tap_mac_address = "c6:ff:ff:ff:0" + std::to_string(first) + ":" + std::to_string(last);
        }
        else
        {
            tap_mac_address = "c6:ff:ff:ff:0" + std::to_string(first) + ":0" + std::to_string(last);
        }
    }
    else if(node_id > 9)
        tap_mac_address = "c6:ff:ff:ff:00:" + std::to_string(node_id);
    else
        tap_mac_address = "c6:ff:ff:ff:00:0" + std::to_string(node_id);
	strcpy(tap_name, tap.c_str());
	persistent_interface = false;
	if (!persistent_interface) 
    {
        //Check if tap is already up
        strcpy(cmd, "ifconfig ");
        strcat(cmd, tap_name);
        strcat(cmd, " > /dev/null 2>&1");
        if(system(cmd) != 0)
        {
            //Get active user
            passwd *user_name;
            user_name = getpwuid(getuid());
            strcpy(user,user_name->pw_name);
            //create tap 
            strcpy(cmd,"ip tuntap add dev ");
            strcat(cmd,tap_name);
            strcat(cmd," mode tap user ");
            strcat(cmd,user);
            if (system(cmd) < 0) perror("system() - /bin/ip");
        } 
        //Set MTU size to 1500 (the default size) in case U1 has been run recently and set it to 244
        strcpy(cmd, "ifconfig ");
        strcat(cmd, tap_name);
        strcat(cmd, " mtu 1500");
        int res = system(cmd);
        if(res < 0)
            printf("system() - ifconfig mtu\n");

        //assign mac address
        strcpy(cmd, "ifconfig ");
        strcat(cmd, tap_name);
        strcat(cmd, " hw ether ");
        strcat(cmd, tap_mac_address.c_str());
        res = system(cmd);
        if(res < 0)
            std::cout << "Error configuring mac address." << std::endl;
        
        //Assing IP address
        strcpy(cmd, "ifconfig ");
        strcat(cmd, tap_name);
        strcat(cmd," ");
        strcat(cmd, tap_ip_address.c_str());
        res = system(cmd);
        if (res < 0) 
            printf("system() - ifconfig\n");

        //Bring up the interface in case it's not up yet
        strcpy(cmd, "ifconfig ");
        strcat(cmd, tap_name);
        strcat(cmd, " up");
        res = system(cmd);
        if (res < 0)
            printf("system() - error bringing interface up\n");


    }   	
    tap_fd = tap_alloc(tap_name, IFF_TAP | IFF_NO_PI); // Tun interface 
    if (tap_fd < 0) {
		printf("Error connecting to tap interface %s\n",tap_name);
		exit(1);
	}
    add_arp_entries(num_nodes_in_net, nodes_in_net);
}

void TunTap::add_arp_entries(unsigned int num_nodes_in_net, unsigned char* nodes_in_net)
{
    std::string mac_address_base = "c6:ff:ff:ff:00:";
    std::string mac_address;
    std::string ip_address_base = "10.10.10.";
    std::string ip_address;
    unsigned char current_node;
    for(unsigned int i = 0; i < num_nodes_in_net; i++)
    {
        current_node = nodes_in_net[i];
        if(current_node != node_id)
        {
            if(current_node > 99)
            {
                int first = current_node/100;
                int last = current_node - 100;
                if(last > 9)
                {
                    mac_address = "c6:ff:ff:ff:0" + std::to_string(first) + ":" + std::to_string(last);
                }
                else
                {
                    mac_address = "c6:ff:ff:ff:0" + std::to_string(first) + ":0" + std::to_string(last);
                }
            }
            else if(current_node > 9)
                mac_address = mac_address_base + std::to_string(current_node);
            else
                mac_address = mac_address_base + "0" + std::to_string(current_node);
            ip_address = ip_address_base + std::to_string(current_node);
            strcpy(cmd, "arp -i tap0 -s ");
            strcat(cmd, ip_address.c_str());
            strcat(cmd, " ");
            strcat(cmd, mac_address.c_str());
            printf("%s\n",cmd);
            int res = system(cmd);
            if(res < 0)
                std::cout << "Error setting arp table for node " << current_node << "." << std::endl;
        }
    }
}
void TunTap::close_interface()
{
	// Detach Tap Interface
	close(tap_fd);

	if (!persistent_interface) {
		strcpy(cmd,"ip tuntap del dev ");
		strcat(cmd,tap_name);
		strcat(cmd," mode tap");
		int res = system(cmd);
        if (res < 0)
        {
            perror("system() - tuntap del");
            std::cout << "error deleting tap" << std::endl;
        }
	}
}
