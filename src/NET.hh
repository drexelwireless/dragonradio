// DWSL - full radio stack

#ifndef NET_HH_
#define NET_HH_

#include <TunTap.hh>
#include <queue>
#include <thread>
#include <stdio.h>

#include "Node.hh"
#include "RadioPacket.hh"

class NET
{
    public:
        // functions
        NET(const std::string& tap_name, NodeId node_id, const std::vector<unsigned char>& nodes_in_net);
        ~NET();
        void readPackets();
        std::unique_ptr<RadioPacket> get_next_packet();

        // other shite
        std::queue<std::unique_ptr<RadioPacket>> tx_packets;
        NodeId node_id;
        TunTap* tt;
        bool continue_reading;
        std::thread readThread;
        unsigned int num_nodes_in_net;
        unsigned int txed_packets;
        PacketId curr_packet_id;
};

#endif
