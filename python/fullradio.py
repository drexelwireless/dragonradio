import argparse
import dragonradio
import signal
import sys

def enumHelp(cls):
    return ', '.join(sorted(cls.__members__.keys()))

def main():
    global done

    parser = argparse.ArgumentParser(description='Run full-radio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-v', action='store_true', dest='verbose',
                        default=False,
                        help='set verbose mode')
    parser.add_argument('-l', action='store', dest='logfile',
                        help='log to file')
    parser.add_argument('--addr', action='store', dest='addr',
                        default='',
                        help='specify device address')
    parser.add_argument('--device', action='store', dest='device',
                        choices=['n210', 'x310'],
                        default='x310',
                        help='specify device type')
    parser.add_argument('--phy', action='store', dest='phy',
                        choices=['flexframe', 'ofdm', 'multiofdm'],
                        default='flexframe',
                        help='set PHY')
    parser.add_argument('-i', action='store', type=int, dest='node_id',
                        default=1,
                        help='set node ID')
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('-f', action='store', type=float, dest='frequency',
                        default=3e9,
                        help='set center frequency (Hz)')
    parser.add_argument('-b', action='store', type=float, dest='bandwidth',
                        default=5e6,
                        help='set bandwidth (Hz)')
    parser.add_argument('-g', '--soft-tx-gain',
                        action='store', type=float, dest='soft_tx_gain',
                        default=-12,
                        help='set soft TX gain (dB)')
    parser.add_argument('-G', '--tx-gain',
                        action='store', type=float, dest='tx_gain',
                        default=25,
                        help='set UHD TX gain (dB)')
    parser.add_argument('-R', '--rx-gain',
                        action='store', type=float, dest='rx_gain',
                        default=25,
                        help='set UHD RX gain (dB)')
    parser.add_argument('-M',
                        action='store', type=int, dest='M',
                        default=48,
                        help='set number of OFDM subcarriers')
    parser.add_argument('-C', '--cp',
                        action='store', type=int, dest='cp_len',
                        default=6,
                        help='set OFDM cyclic prefix length')
    parser.add_argument('-T', '--taper',
                        action='store', type=int, dest='taper_len',
                        default=4,
                        help='set OFDM taper length')
    parser.add_argument('-m', '--mod',
                        action='store', type=dragonradio.ModulationScheme, dest='ms',
                        default='qpsk',
                        help='set modulation scheme: ' + enumHelp(dragonradio.ModulationScheme))
    parser.add_argument('-c', '--fec0',
                        action='store', type=dragonradio.FECScheme, dest='fec0',
                        default='v29',
                        help='set inner FEC: ' + enumHelp(dragonradio.FECScheme))
    parser.add_argument('-k', '--fec1',
                        action='store', type=dragonradio.FECScheme, dest='fec1',
                        default='rs8',
                        help='set outer FEC: ' + enumHelp(dragonradio.FECScheme))
    parser.add_argument('-r', '--check',
                        action='store', type=dragonradio.CRCScheme, dest='crc',
                        default='crc32',
                        help='set data validity check: ' + enumHelp(dragonradio.CRCScheme))
    args = parser.parse_args()

    # See:
    #   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
    # Not applying recommended TX/RX gains yet...
    # args.tx_gain = 23
    # args.rx_gain = 8

    #dragonradio.rc = dragonradio.RadioConfig()
    dragonradio.rc.verbose = args.verbose

    if args.logfile:
        dragonradio.logger = dragonradio.Logger(args.logfile)
        dragonradio.logger.setAttribute('node_id', args.node_id);
        dragonradio.logger.setAttribute('frequency', args.frequency);
        dragonradio.logger.setAttribute('soft_tx_gain', args.soft_tx_gain);
        dragonradio.logger.setAttribute('tx_gain', args.tx_gain);
        dragonradio.logger.setAttribute('rx_gain', args.rx_gain);
        dragonradio.logger.setAttribute('M', args.M);
        dragonradio.logger.setAttribute('cp_len', args.cp_len);
        dragonradio.logger.setAttribute('taper_len', args.taper_len);

    if args.device == 'x310':
        x310 = True
    else:
        x310 = False

    tx_antenna = 'TX/RX'
    if x310:
        rx_antanna = 'RX2'
    else:
        rx_antanna = 'TX/RX'

    usrp = dragonradio.USRP(args.addr,
                            x310,
                            args.frequency,
                            tx_antenna,
                            rx_antanna,
                            args.tx_gain,
                            args.rx_gain)

    net = dragonradio.Net('tap0', '10.10.10.%d', 'c6:ff:ff:ff:%02x', args.node_id)

    for i in range(0, args.num_nodes):
        net.addNode(i+1)

    min_packet_size = 512

    if args.phy == 'flexframe':
        phy = dragonradio.FlexFrame(net, min_packet_size)
    elif args.phy == 'ofdm':
        phy = dragonradio.OFDM(net, args.N, args.cp_len, args.taper_len, min_packet_size)
    elif args.phy == 'multiofdm':
        phy = dragonradio.OFDM(net, args.N, args.cp_len, args.taper_len, min_packet_size)
    else:
        print('Bad PHY: {}'.format(args.phy), file=sys.stderr)
        sys.exit(1)

    num_modulation_threads = 2
    num_demodulation_threads = 10
    ordered_demodulation = True

    modulator = dragonradio.ParallelPacketModulator(net, phy, num_modulation_threads)

    demodulator = dragonradio.ParallelPacketDemodulator(net, phy, ordered_demodulation, num_demodulation_threads)

    # slot size *including* guard (seconds)
    slot_size = .035
    guard_size = .01;

    mac = dragonradio.TDMA(usrp, phy, modulator, demodulator,
                           args.bandwidth,
                           len(net),
                           slot_size,
                           guard_size);

    mac[net.my_node_id - 1] = True

    # Wait for Ctrl-C
    signal.sigwait([signal.SIGINT])

    # We don't need to explicitly destroy these objects
    if False:
        del mac
        del phy
        del modulator
        del demodulator
        del net
        del usrp

    return 0

if __name__ == '__main__':
    main()
