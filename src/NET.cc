// DWSL - full radio stack

#include "NET.hh"

NET::NET(const std::string& tap_name, unsigned int node_id, unsigned int num_nodes_in_net, unsigned char* nodes_in_net)
{
    printf("Creating NETWORK\n");
    this->node_id = node_id;
    this->num_nodes_in_net = num_nodes_in_net;
    this->nodes_in_net = nodes_in_net;
    this->tt = new TunTap(tap_name, node_id, num_nodes_in_net, nodes_in_net);
    this->continue_reading = true;
    this->readThread = std::thread(&NET::readPackets,this);
    this->txed_packets = 0;
    this->curr_packet_id = 0;
}

NET::~NET()
{
    continue_reading = false;
    printf("Closing Network\n");
    delete tt;
}

void NET::readPackets()
{
    unsigned int dest_id = 0;
    while(continue_reading)
    {
        unsigned char* data = new unsigned char[10000];
        unsigned int total = tt->cread((char*)data,10000);
        if(total>0)
        {
            dest_id = data[33];
            if(dest_id>0)
            {
                tx_packet_t tx_packet;
                tx_packet.payload_size = total;
                tx_packet.destination_id = dest_id;
                tx_packet.payload = &data[0];
                tx_packet.packet_id = curr_packet_id;
                tx_packets.push(tx_packet);
                curr_packet_id++;
            }
        }
    }
}

tx_packet_t* NET::get_next_packet()
{
    if(!tx_packets.empty())
    {
        tx_packet_t it = tx_packets.front();
        tx_packets.pop();
        if(it.packet_id==txed_packets)
        {
            tx_packet_t* new_packet = new tx_packet_t;
            new_packet->payload = it.payload;
            new_packet->payload_size = it.payload_size;
            new_packet->destination_id = it.destination_id;
            new_packet->packet_id = it.packet_id;
            txed_packets++;
            return new_packet;
        }
        else
        {
            tx_packet_t* null_packet = new tx_packet_t;
            null_packet->payload_size = 0;
            return null_packet;
        }
    }
    else
    {
        tx_packet_t* null_packet = new tx_packet_t;
        null_packet->payload_size = 0;
        return null_packet;
    }

}
