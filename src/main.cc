// DWSL - full radio stack

#include <sys/types.h>
#include <signal.h>

#include <stdio.h>
#include <unistd.h>
#include <thread>

#include "Logger.hh"
#include "ParallelPacketModulator.hh"
#include "ParallelPacketDemodulator.hh"
#include "RadioConfig.hh"
#include "USRP.hh"
#include "phy/FlexFrame.hh"
#include "phy/MultiOFDM.hh"
#include "phy/OFDM.hh"
#include "mac/TDMA.hh"
#include "net/Net.hh"

void usage(void)
{
    printf("fullradio [OPTION]\n");
    printf("\n");
    printf("  h/?   : help\n");
    printf("  q/v   : quiet/verbose\n");
    printf("  o     : use OFDM PHY\n");
    printf("  u     : use multichannel OFDM PHY\n");
    printf("  l     : log to file\n");
    printf("  a     : set device address\n");
    printf("  2     : this is an N210\n");
    printf("  3     : this is a X310 (default)\n");
    printf("  i     : node ID,                default:    1\n");
    printf("  n     : nodes in net,           default:    2\n");
    printf("  f     : center frequency [Hz],  default:    3 GHz\n");
    printf("  b     : bandwidth [Hz],         default:    5 MHz\n");
    printf("  g     : software tx gain [dB],  default:  -12 dB \n");
    printf("  G     : uhd tx gain [dB],       default:   25 dB\n");
    printf("  R     : uhd rx gain [dB],       default:   25 dB\n");
    printf("  M     : number of subcarriers,  default:   48\n");
    printf("  C     : cyclic prefix length,   default:    6\n");
    printf("  T     : taper length,           default:    4\n");
    printf("  m     : modulation scheme,      default: qpsk\n");
    liquid_print_modulation_schemes();
    printf("  c     : coding scheme (inner),  default:  v29\n");
    printf("  k     : coding scheme (outer),  default:  rs8\n");
    liquid_print_fec_schemes();
    printf("  r     : CRC scheme,             default:  crc32\n");
    liquid_print_crc_schemes();
}

int main(int argc, char** argv)
{
    bool use_ofdm = false;
    bool use_multi = false;
    const char* logfile = NULL;
    bool x310 = true;

    unsigned int node_id = 1;           // must be in {1,...,num_nodes_in_net}
    unsigned int num_nodes = 2;         // number of nodes in network

    double frequency = 3e9;             // carrier frequency
    double bandwidth = 5e6;             // bandwidth
    double uhd_txgain = 25.0;           // uhd (hardware) tx gain [dB]
    double uhd_rxgain = 25.0;           // uhd (hardware) rx gain [dB]

    // OFDM parameters
    unsigned int M = 48;                // number of subcarriers
    unsigned int cp_len = 6;            // cyclic prefix length
    unsigned int taper_len = 4;         // taper length

    std::string addr; // Device address

    // TODO
    // make these things CLI configurable
    unsigned int min_packet_size = 512;      // minimum radio packet size
    double slot_size = .035;                 // slot size *including* guard (seconds)
    double guard_size = .01;                 // inter-slot guard time (sec)
    unsigned int nmodthreads = 2;            // number of threads available for modulation
    unsigned int ndemodthreads = 10;         // number of threads available for demodulation
    bool ordered = true;                     // Force ordered demodulation

    rc = std::make_shared<RadioConfig>();

    int ch;

    while ((ch = getopt(argc,argv,"hqvoul:a:23i:n:f:b:g:G:N:M:C:T:P:m:c:k:")) != EOF) {
        switch (ch) {
        case '?':
        case 'h':   usage();                            return 0;
        case 'q':   rc->verbose     = false;            break;
        case 'v':   rc->verbose     = true;             break;
        case 'o':   use_ofdm        = true;             break;
        case 'u':   use_multi       = true;             break;
        case 'l':   logfile         = optarg;           break;
        case 'a':   addr            = optarg;           break;
        case '2':   x310            = false;            break;
        case '3':   x310            = true;             break;
        case 'i':   node_id         = atoi(optarg);     break;
        case 'n':   num_nodes       = atoi(optarg);     break;
        case 'f':   frequency       = atof(optarg);     break;
        case 'b':   bandwidth       = atof(optarg);     break;
        case 'g':   rc->soft_txgain = atof(optarg);     break;
        case 'G':   uhd_txgain      = atof(optarg);     break;
        case 'R':   uhd_rxgain      = atof(optarg);     break;
        case 'M':   M               = atoi(optarg);     break;
        case 'C':   cp_len          = atoi(optarg);     break;
        case 'T':   taper_len       = atoi(optarg);     break;
        case 'm':   rc->ms          = liquid_getopt_str2mod(optarg);    break;
        case 'c':   rc->fec0        = liquid_getopt_str2fec(optarg);    break;
        case 'k':   rc->fec1        = liquid_getopt_str2fec(optarg);    break;
        case 'r':   rc->check       = liquid_getopt_str2crc(optarg);    break;
        default:    usage();                            return 0;
        }
    }

    if (cp_len == 0 || cp_len > M) {
        fprintf(stderr,"error: %s, cyclic prefix must be in (0,M]\n", argv[0]);
        exit(1);
    } else if (rc->ms == LIQUID_MODEM_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported mod. scheme\n", argv[0]);
        exit(-1);
    } else if (rc->fec0 == LIQUID_FEC_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported inner fec scheme\n", argv[0]);
        exit(-1);
    } else if (rc->fec1 == LIQUID_FEC_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported outer fec scheme\n", argv[0]);
        exit(-1);
    } else if (rc->check == LIQUID_CRC_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported check scheme\n", argv[0]);
        exit(-1);
    }

    argc -= optind;
    argv += optind;

    ///////////////////////////////////////////////////////////////////////////////////////

    // See:
    //   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
    // Not applying recommended TX/RX gains yet...
    //tx_gain = 23;
    //rx_gain = 8;

    auto usrp = std::make_shared<USRP>(addr,
                                       x310,
                                       frequency,
                                       "TX/RX",
                                       x310 ? "RX2" : "TX/RX",
                                       uhd_txgain,
                                       uhd_rxgain);

    if (logfile) {
        Clock::time_point t_start = Clock::time_point(Clock::now().get_full_secs());

        logger = std::make_shared<Logger>(t_start);
        logger->open(logfile);
        logger->setAttribute("start", (uint32_t) t_start.get_full_secs());
        logger->setAttribute("node_id", node_id);
        logger->setAttribute("frequency", frequency);
        logger->setAttribute("soft_tx_gain", rc->soft_txgain);
        logger->setAttribute("tx_gain", uhd_txgain);
        logger->setAttribute("rx_gain", uhd_rxgain);
        logger->setAttribute("M", M);
        logger->setAttribute("cp_len", cp_len);
        logger->setAttribute("taper_len", taper_len);
    }

    auto net = std::make_shared<Net>("tap0", "10.10.10.%d", "c6:ff:ff:ff:%02x", node_id);

    for (NodeId i = 1; i <= num_nodes; i++)
      net->addNode(i);

    std::shared_ptr<PHY> phy;

    if (use_ofdm)
        phy = std::make_shared<OFDM>(net, M, cp_len, taper_len, nullptr, min_packet_size);
    else if (use_multi)
        phy = std::make_shared<MultiOFDM>(net, M, cp_len, taper_len, nullptr, min_packet_size);
    else
        phy = std::make_shared<FlexFrame>(net, min_packet_size);

    auto modulator = std::make_shared<ParallelPacketModulator>(net, phy, nmodthreads);

    auto demodulator = std::make_shared<ParallelPacketDemodulator>(net, phy, ordered, ndemodthreads);

    auto mac = std::make_shared<TDMA>(usrp, phy, modulator, demodulator, bandwidth, net->size(), slot_size, guard_size);

    mac->addSlot(net->getMyNodeId() - 1);

    // Wait for Ctrl-C
    sigset_t waitset;
    int      sig;

    sigemptyset(&waitset);
    sigaddset(&waitset, SIGINT);

    sigprocmask(SIG_BLOCK, &waitset, NULL);

    sigwait(&waitset, &sig);

    printf("Done!\n");
}
