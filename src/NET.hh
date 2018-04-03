// DWSL - full radio stack

#ifndef NET_HH_
#define NET_HH_

#include <TunTap.hh>
#include <queue>
#include <thread>
#include <stdio.h>

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
        NET(const std::string& tap_name, unsigned int node_id, unsigned int num_nodes_in_net, unsigned char* nodes_in_net);
        ~NET();
        void readPackets();
        tx_packet_t* get_next_packet();

        // other shite
        std::queue<tx_packet_t> tx_packets;
        unsigned int node_id;
        TunTap* tt;
        bool continue_reading;
        std::thread readThread;
        unsigned int num_nodes_in_net;
        unsigned char* nodes_in_net;
        unsigned int txed_packets;
        unsigned int curr_packet_id;
};

#endif
