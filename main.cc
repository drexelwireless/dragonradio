// DWSL - full radio stack

#include <MACPHY.hh>
#include <NET.hh>
#include <stdio.h>
#include <unistd.h>
#include <thread>

int main()
{
    // TODO
    // make these things CLI configurable
    double center_freq = 1340e6;                // Hz
    double bandwidth = 5e6;                    // Hz
    unsigned int padded_bytes = 512;            // bytes to add to each paylaod
    float tx_gain = 25;                         // dB
    float rx_gain = 25;                         // dB
    unsigned int node_id = 1;                   // must be in {1,...,num_nodes_in_net}
    unsigned int num_nodes_in_net = 2;          // number of nodes in network
    double frame_size = .07;                     // slot_size*num_nodes_in_net (seconds)
    unsigned int rx_thread_pool_size = 4;       // number of threads available for demodulation
    float pad_size = .01;                       // inter slot dead time
    unsigned int packets_per_slot = 2;          // how many packets to stuff into each slot

    ///////////////////////////////////////////////////////////////////////////////////////

    unsigned char nodes_in_net[num_nodes_in_net];
    for(unsigned int i=0;i<num_nodes_in_net;i++)
    {
        nodes_in_net[i] = i+1;
    }

    NET net("tap0",node_id,num_nodes_in_net,&nodes_in_net[0]);
    MACPHY mp(&net,center_freq,bandwidth,padded_bytes,tx_gain,rx_gain,frame_size,rx_thread_pool_size,pad_size,packets_per_slot);

    // start the rx thread
    std::thread rx_worker_thread = std::thread(rx_worker,rx_thread_pool_size);

    // use main thread for tx_worker
    mp.readyOFDMBuffer();
    while(mp.continue_running)
    {
        mp.TX_TDMA_OFDM();
    }

    rx_worker_thread.join();
    
    printf("Done\n");
}
