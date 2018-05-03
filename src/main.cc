// DWSL - full radio stack

#include <sys/types.h>
#include <signal.h>

#include <stdio.h>
#include <unistd.h>
#include <thread>

#include "Logger.hh"
#include "MAC.hh"
#include "NET.hh"
#include "ParallelPacketModulator.hh"
#include "ParallelPacketDemodulator.hh"
#include "USRP.hh"
#include "phy/FlexFrame.hh"
#include "phy/MultiOFDM.hh"
#include "phy/OFDM.hh"

void usage(void)
{
}

int main(int argc, char** argv)
{
    // TODO
    // make these things CLI configurable
    double center_freq = 1340e6;                // Hz
    double bandwidth = 5e6;                     // Hz
    unsigned int min_packet_size = 512;         // minimum radio packet size
    float tx_gain = 25;                         // dB
    float rx_gain = 25;                         // dB
    unsigned int node_id = 1;                   // must be in {1,...,num_nodes_in_net}
    unsigned int num_nodes_in_net = 2;          // number of nodes in network
    double frame_size = .07;                    // slot_size*num_nodes_in_net (seconds)
    double guard_size = .01;                    // inter-slot guard time (sec)
    unsigned int rx_thread_pool_size = 10;       // number of threads available for demodulation
    bool x310 = true;                           // is this an x310
    bool multichannel = false;                  // Should we use multichannel code?
    bool ofdm = false;                         // Should we use OFDM code?
    const char* logfile = NULL;
    std::string addr;

    int ch;

    while ((ch = getopt(argc, argv, "23a:l:mn:o")) != -1) {
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

        case 'l':
          logfile = optarg;
          break;

        case 'm':
          multichannel = true;
          break;

        case 'n':
          node_id = atoi(optarg);
          printf("node_id = %d\n", node_id);
          break;

        case 'o':
          ofdm = true;
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

    auto usrp = std::make_shared<USRP>(addr, x310, center_freq, "TX/RX", x310 ? "RX2" : "TX/RX", tx_gain, rx_gain);

    if (logfile) {
        logger = std::make_shared<Logger>();
        logger->open(logfile);
        logger->setNodeId(node_id);
    }

    auto net = std::make_shared<NET>("tap0",node_id,nodes_in_net);

    std::shared_ptr<PHY> phy;

    if (ofdm)
        phy = std::make_shared<OFDM>(net, 48, 6, 4, nullptr, min_packet_size);
    else if (multichannel)
        phy = std::make_shared<MultiOFDM>(net, 48, 6, 4, nullptr, min_packet_size);
    else
        phy = std::make_shared<FlexFrame>(net, min_packet_size);

    auto modulator = std::make_shared<ParallelPacketModulator>(net, phy);

    auto demodulator = std::make_shared<ParallelPacketDemodulator>(net, phy, false, rx_thread_pool_size);

    auto mac = std::make_shared<MAC>(usrp, net, phy, modulator, demodulator, bandwidth, frame_size, guard_size);

    if (logger) {
        logger->setTXBandwidth(bandwidth*phy->getTxRateOversample());
        logger->setRXBandwidth(bandwidth*phy->getRxRateOversample());
    }

    // Wait for Ctrl-C
    sigset_t waitset;
    int      sig;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIGINT);

    sigprocmask(SIG_BLOCK, &waitset, NULL);

    sigwait(&waitset, &sig);

    net->stop();
    mac->stop();
    modulator->stop();
    demodulator->stop();
    if (logger) {
        logger->stop();
        logger.reset();
    }

    printf("Done!\n");
}
