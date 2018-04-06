// DWSL - full radio stack

#ifndef NET_HH_
#define NET_HH_

#include <TunTap.hh>
#include <queue>
#include <thread>
#include <stdio.h>

#include "Node.hh"

typedef struct
{
    unsigned int packet_id;
    unsigned int payload_size;
    unsigned int destination_id;
    unsigned char* payload;

} tx_packet_t;

class NET
{
    public:
        // functions
        NET(const std::string& tap_name, NodeId node_id, const std::vector<unsigned char>& nodes_in_net);
        ~NET();
        void readPackets();
        tx_packet_t* get_next_packet();

        // other shite
        std::queue<tx_packet_t> tx_packets;
        NodeId node_id;
        TunTap* tt;
        bool continue_reading;
        std::thread readThread;
        unsigned int num_nodes_in_net;
        unsigned int txed_packets;
        unsigned int curr_packet_id;
};

#endif
