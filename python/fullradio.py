import argparse
import IPython
import signal
import sys

import dragonradio

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
    parser.add_argument('--rx-antenna', action='store', dest='rx_antenna',
                        default='RX2',
                        help='set RX antenna')
    parser.add_argument('--tx-antenna', action='store', dest='tx_antenna',
                        default='TX/RX',
                        help='set TX antenna')
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
    parser.add_argument('--auto-soft-tx-gain',
                        action='store_true', dest='auto_soft_tx_gain',
                        default=False,
                        help='automatically choose soft TX gain')
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
                        action='store', type=dragonradio.CRCScheme, dest='check',
                        default='crc32',
                        help='set data validity check: ' + enumHelp(dragonradio.CRCScheme))
    parser.add_argument('--interactive',
                        action='store_true', dest='interactive',
                        help='enter interactive shell after radio is configured')

    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # See:
    #   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
    # Not applying recommended TX/RX gains yet...
    # args.tx_gain = 23
    # args.rx_gain = 8

    #dragonradio.rc = dragonradio.RadioConfig()
    dragonradio.rc.verbose = args.verbose
    dragonradio.rc.soft_txgain = args.soft_tx_gain
    dragonradio.rc.ms = args.ms
    dragonradio.rc.check = args.check
    dragonradio.rc.fec0 = args.fec0
    dragonradio.rc.fec1 = args.fec1

    usrp = dragonradio.USRP(args.addr,
                            args.frequency,
                            args.tx_antenna,
                            args.rx_antenna,
                            args.tx_gain,
                            args.rx_gain)

    # Create the logger *after* we create the USRP so that we have a global
    # clock
    if args.logfile:
        logger = dragonradio.Logger(args.logfile)
        logger.setAttribute('node_id', args.node_id)
        logger.setAttribute('frequency', args.frequency)
        logger.setAttribute('soft_tx_gain', args.soft_tx_gain)
        logger.setAttribute('tx_gain', args.tx_gain)
        logger.setAttribute('rx_gain', args.rx_gain)
        logger.setAttribute('M', args.M)
        logger.setAttribute('cp_len', args.cp_len)
        logger.setAttribute('taper_len', args.taper_len)
        dragonradio.Logger.singleton = logger

    net = dragonradio.Net('tap0', '10.10.10.%d', 'c6:ff:ff:ff:ff:%02x', args.node_id)

    for i in range(0, args.num_nodes):
        net.addNode(i+1)

    if args.auto_soft_tx_gain:
        for node in net:
            net[node].desired_soft_tx_gain = 0.0

    min_packet_size = 512

    if args.phy == 'flexframe':
        phy = dragonradio.FlexFrame(net, min_packet_size)
    elif args.phy == 'ofdm':
        phy = dragonradio.OFDM(net, args.M, args.cp_len, args.taper_len, min_packet_size)
    elif args.phy == 'multiofdm':
        phy = dragonradio.OFDM(net, args.M, args.cp_len, args.taper_len, min_packet_size)
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
    guard_size = .01

    mac = dragonradio.TDMA(usrp, phy, modulator, demodulator,
                           args.bandwidth,
                           len(net),
                           slot_size,
                           guard_size)

    if args.logfile:
        logger.setAttribute('tx_bandwidth', usrp.tx_rate)
        logger.setAttribute('rx_bandwidth', usrp.rx_rate)

    mac[net.my_node_id - 1] = True

    if args.interactive:
        IPython.embed()

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
