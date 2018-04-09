// DWSL - full radio stack

#include <sys/types.h>
#include <signal.h>

#include <MAC.hh>
#include <NET.hh>
#include <PHY.hh>
#include <USRP.hh>
#include <stdio.h>
#include <unistd.h>
#include <thread>

void usage(void)
{
}

int main(int argc, char** argv)
{
    // TODO
    // make these things CLI configurable
    double center_freq = 1340e6;                // Hz
    double bandwidth = 5e6;                    // Hz
    unsigned int min_packet_size = 512;        // minimum radio packet size
    float tx_gain = 25;                         // dB
    float rx_gain = 25;                         // dB
    unsigned int node_id = 1;                   // must be in {1,...,num_nodes_in_net}
    unsigned int num_nodes_in_net = 2;          // number of nodes in network
    double frame_size = .07;                     // slot_size*num_nodes_in_net (seconds)
    unsigned int rx_thread_pool_size = 4;       // number of threads available for demodulation
    float pad_size = .01;                       // inter slot dead time
    bool x310 = true;                           // is this an x310
    std::string addr;

    int ch;

    while ((ch = getopt(argc, argv, "23a:n:")) != -1) {
      switch (ch) {
        case '2':
          x310 = false;
          break;

        case '3':
          x310 = true;
          break;

        case 'a':
          addr = optarg;
          break;

        case 'n':
          node_id = atoi(optarg);
          printf("node_id = %d\n", node_id);
          break;

        case '?':
        default:
        usage();
      }
    }

    argc -= optind;
    argv += optind;

    ///////////////////////////////////////////////////////////////////////////////////////

    std::vector<unsigned char> nodes_in_net(num_nodes_in_net);

    for(unsigned int i=0;i<num_nodes_in_net;i++)
    {
        nodes_in_net[i] = i+1;
    }

    // See:
    //   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
    // Not applying recommended TX/RX gains yet...
    if (x310) {
        center_freq = 3e9;
        //tx_gain = 23;
        //rx_gain = 8;
    }

    std::shared_ptr<USRP> usrp(new USRP(addr, x310, center_freq, "TX/RX", x310 ? "RX2" : "TX/RX", tx_gain, rx_gain));
    std::shared_ptr<NET>  net(new NET("tap0",node_id,nodes_in_net));
    std::shared_ptr<PHY>  phy(new PHY(usrp, net, bandwidth, min_packet_size, rx_thread_pool_size));
    std::shared_ptr<MAC>  mac(new MAC(usrp, net, phy, frame_size, pad_size));

    // Wait for Ctrl-C
    sigset_t waitset;
    int      sig;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIGINT);

    sigprocmask(SIG_BLOCK, &waitset, NULL);

    sigwait(&waitset, &sig);

    net->join();
    phy->join();
    mac->join();

    printf("Done!\n");
}
